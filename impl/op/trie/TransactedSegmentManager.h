#ifndef _OP_VTM_TRANSACTEDSEGMENTMANAGER__H_
#define _OP_VTM_TRANSACTEDSEGMENTMANAGER__H_

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#include <op/trie/SegmentManager.h>
#include <op/trie/Transactional.h>
#include <op/trie/Exceptions.h>
#include <unordered_map>
#include <memory>
#include <deque>
#include <thread>

namespace OP
{
    using namespace vtm;
    namespace trie{
        
        class TransactedSegmentManager :
            public SegmentManager
        {
            friend struct SegmentManager;
        public:
            ~TransactedSegmentManager()
            {
            }
            transaction_ptr_t begin_transaction() override
            {
                guard_t g(&_opened_transactions_lock);
                auto insres = _opened_transactions.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(std::this_thread::get_id()),
                    std::forward_as_tuple());
                if (insres.second) //just insert
                {
                    insres.first->second = transaction_impl_ptr_t(
                        new TransactionImpl(_transaction_uid_gen.fetch_add(1), this),
                        _transaction_deleter);
                }
                else
                    insres.first->second->recursive();
                return insres.first->second;
            }
            
            ReadonlyMemoryRange readonly_block(FarAddress pos, segment_pos_t size) override
            {
                RWR search_range(pos, size);
                guard_t g2(&_map_lock); //trick - capture ownership in this thread, but delegate find in other one
                //@! need hard test to ensure this parallelism has a benefit in compare with _captured_blocks.emplace/.insert
                std::future<captured_blocks_t::iterator> real_mem_future = std::async(std::launch::async, [&](){ 
                    return _captured_blocks.lower_bound(search_range);
                });
                transaction_impl_ptr_t current;
                if (true)
                {  //extract current transaction in safe way
                    guard_t g1(&_opened_transactions_lock);
                    auto found = _opened_transactions.find(std::this_thread::get_id());
                    if (found != _opened_transactions.end()) 
                    {
                        current = found->second;
                    }
                }
                if (!current) //no current transaction at all
                {
                    //check presence of queried range in shadow buffers
                    auto found_res = real_mem_future.get();
                    if(found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first))) 
                    {
                        // range exists. 
                        if (!found_res->first.is_included(search_range))// don't allow trnsactions on overlapped memory blocks
                            throw Exception(er_overlapping_block);
                        if (found_res->second.transaction_code().flag & wr_c) //some write-tran already captured this block
                            throw ConcurentLockException();
                        if (found_res->second.shadow_buffer() != nullptr)
                        {
                            auto dif_off = pos.diff(found_res->first.pos());
                            return ReadonlyMemoryRange(
                                found_res->second.shadow_buffer() + dif_off,
                                size,
                                std::move(pos),
                                std::move(SegmentManager::get_segment(pos.segment))
                            );
                        }
                        //no shadow buffer, hence block used for RO only
                    }
                    //no shadow buffer for this region, just get RO memory
                    return SegmentManager::readonly_block(pos, size);
                }
                
                auto found_res = real_mem_future.get();
                if (found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first)))
                {//result found
                    if (!found_res->first.is_included(search_range))// don't allow transactions on overlapped memory blocks
                        throw Exception(er_overlapping_block);
                    //add hashcode to scope of transactions, may rise ConcurentLockException
                    if (found_res->second.permit_read(current->transaction_id()))
                    {//new lock was obtained, need persist to the transaction scope
                        current->store(found_res);
                    }
                }
                else//new entry, so no conflicts, place entry & return result
                {
                    found_res = _captured_blocks.emplace_hint(
                        found_res, //use previous found pos as ahint 
                        std::piecewise_construct, 
                        std::forward_as_tuple(search_range),
                        std::forward_as_tuple(ro_c, current->transaction_id())
                    );
                    current->store(found_res);
                }
                //Following 'if' checks only shadow-buffer without transaction_code().flag & optimistic_write_c
                //because if write=access accured the same transaction must always see changes
                //other transaction should fail on obtaining lock, so shadow-buffer is enough to check
                if ( found_res->second.shadow_buffer() == nullptr //no shadow buffer for this region, just get RO memory
                    )
                    return SegmentManager::readonly_block(pos, size);
                return ReadonlyMemoryRange(found_res->second.shadow_buffer(), //use shadow
                    size, std::move(pos), std::move(SegmentManager::get_segment(pos.segment)));
            }
            MemoryRange writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
            {
                RWR search_range(pos, size);
                guard_t g2(&_map_lock); //trick - capture ownership in this thread, but delegate find in other one
                //@! need hard test to ensure this parallelism has a benefit in compare with _captured_blocks.emplace/.insert
                std::future<captured_blocks_t::iterator> real_mem_future = std::async(std::launch::async, [&](){ 
                    return _captured_blocks.lower_bound(search_range);
                });
                transaction_impl_ptr_t current;
                if (true)
                {  //extract current transaction in safe way
                    guard_t g1(&_opened_transactions_lock);
                    auto found = _opened_transactions.find(std::this_thread::get_id());
                    if (found != _opened_transactions.end()) 
                    {
                        current = found->second;
                    }
                }
                if (!current)//no current transaction at all
                    throw Exception(er_transaction_not_started, "write allowed only inside transaction");
                auto found_res = real_mem_future.get();
                if (found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first)))
                {//block already have associated transaction(s)
                    // don't allow transactions on overlapped memory blocks
                    if (!found_res->first.is_included(search_range))
                        throw Exception(er_overlapping_block);
                    //only exclusive access is permitted
                    if (!found_res->second.is_exclusive_access(current->transaction_id()))
                        throw ConcurentLockException();
                    //may be block just for read
                    if (!(found_res->second.transaction_code().flag & wr_c))
                    {//obtain write lock over already existing read-lock
                         auto super_res = SegmentManager::writable_block(pos, size, hint);
                        //clone memory block, so other could see old result
                        found_res->second.create_shaddow(true, //this mean create buffer for read (optimistic write)
                            super_res.pos(),
                            super_res.count(),
                            hint
                            );
                        //Don't need to current->store, because read-transaction already in stock
                        //current->store(found_res);
                        current->on_shadow_buffer_created(found_res->second.shadow_buffer(), found_res->first.pos());
                    }
                }
                else
                { //need insert new block
                    found_res = 
                        _captured_blocks.emplace_hint(
                            found_res, //use previous found pos as ahint  
                            std::piecewise_construct,
                            std::forward_as_tuple(search_range),
                            std::forward_as_tuple(wr_c, current->transaction_id())
                    );
                    auto super_res = SegmentManager::writable_block(pos, size, hint);
                    //clone memory block, so other could see old result
                    found_res->second.create_shaddow(false, //this mean create buffer for write (instead of buffer for read)
                        super_res.pos(),
                        super_res.count(),
                        hint
                        );
                    current->store(found_res);
                }
                
                return MemoryRange( found_res->second.shadow_buffer(), size,
                        std::move(pos), std::move(SegmentManager::get_segment(pos.segment)) );
            }
            MemoryRange upgrade_to_writable_block(ReadonlyMemoryRange& ro)  override
            {
                return this->writable_block(ro.address(), ro.count());
            }
            far_pos_t to_far(segment_idx_t segment, const void * address) override
            {
                transaction_impl_ptr_t current;
                if (true)
                {  //extract current transaction in safe way
                    guard_t g1(&_opened_transactions_lock);
                    auto found = _opened_transactions.find(std::this_thread::get_id());
                    if (found == _opened_transactions.end()) 
                    {//no current transaction at all
                        return SegmentManager::to_far(segment, address);
                    }
                    current = found->second;
                }
                //from current transaction take address
                far_pos_t result;
                if (current->get_address_by_shadow(reinterpret_cast<const std::uint8_t*>(address), result)) //yes, there is a shadow
                    return result;
                //no shaddow buffer, return the origin
                return SegmentManager::to_far(segment, address);
            }

        protected:
            TransactedSegmentManager(const char * file_name, bool create_new, bool readonly) :
                SegmentManager(file_name, create_new, readonly),
                _transaction_deleter(this)
            {
                _transaction_nesting = 0;
            }
            virtual std::shared_ptr<Transaction> get_current_transaction()
            {
                //this method can be replaced if compiler supports 'thread_local' keyword
                guard_t g(&_opened_transactions_lock);
                auto found = _opened_transactions.find(std::this_thread::get_id());
                return found == _opened_transactions.end() ? std::shared_ptr<Transaction>() : found->second;
            }
        private:
            /**Write explictly to segment manager*/
            void raw_write(const FarAddress& fp, const std::uint8_t *buffer, std::uint32_t size)
            {
                auto s = get_segment(fp.segment);
                memcpy(s->at<std::uint8_t>(fp.offset), buffer, size);
            }
            typedef OP::Range<FarAddress, segment_pos_t> RWR;
            enum flag_t : segment_pos_t
            {
                ro_c    = 0x0,
                wr_c    = 0x1,
                /**
                * When lock is upgraded from ro_c to wr_c const data pointer can be in use, so 
                * write block must return the same pointer. To support this feature instead of creating 
                * shadow buffer on write, the shadow buffer for read is created.
                * This flag used for commit/rollback policy distinguish either to optimistic commit or to
                * optimistic rollback
                */
                optimistic_write_c = 0x2
            };
            
            union TransactionCode
            {
                std::uint64_t align;
                struct
                {
                    std::uint64_t transactions_count : 16;
                    std::uint64_t flag : 2;
                    std::uint64_t hash : 46;
                };
                TransactionCode() :
                    align(0){}
                TransactionCode(flag_t capture_kind, Transaction::transaction_id_t id) :
                    transactions_count(1),
                    flag(capture_kind),
                    hash(transaction_id_to_hash(id))
                    {}
                static std::uint64_t transaction_id_to_hash(Transaction::transaction_id_t id)
                {
                    //take 47 bit only
                    return (101LLU * id) & ((1LLU << 46) - 1);
                }

            };
            /**Describe how memory block is used*/
            struct BlockUse 
            {
                BlockUse(flag_t capture_kind, Transaction::transaction_id_t id):
                    _transaction_code(capture_kind, id),
                    _shadow_buffer(nullptr)
                {
                    
                }
                ~BlockUse()
                {
                    if (_shadow_buffer)
                        delete[]_shadow_buffer;
                }
                void create_shaddow(bool is_optimistic_write, const std::uint8_t* from, segment_pos_t size,
                    WritableBlockHint hint = WritableBlockHint::block_no_hint_c)
                {
                    assert(_shadow_buffer == nullptr);
                    _shadow_buffer = new std::uint8_t[size];

                    if ( hint != WritableBlockHint::block_for_write_c) //write only don't need create copy
                        memcpy(_shadow_buffer, from, size);

                    if (is_optimistic_write)
                        _transaction_code.flag |= optimistic_write_c;
                    else
                        _transaction_code.flag &= ~optimistic_write_c;
                }
                bool is_exclusive_access(Transaction::transaction_id_t current) const
                {
                    //use Bloom filter
                    //auto c = TransactionCode::transaction_id_to_hash(current);
                    TransactionCode test(wr_c, current);
                    if (_transaction_code.align == test.align)
                        return true;
                    //check if ro-access for 1 thread only
                    test.flag = ro_c;
                    return _transaction_code.align == test.align;
                }
                /**Mark this block as available for multiple read operations. 
                * If current transaction already has write 
                * access, nothing happens.
                * @return true if lock was accured, false mean that lock was already obtained before
                * @throws ConcurentLockException if other transaction owns write-lock
                */
                bool permit_read(Transaction::transaction_id_t current) 
                {
                    //@! to review: atomic XOR
                    std::uint64_t origin = _transaction_code.align;
                    auto current_hash = TransactionCode::transaction_id_to_hash(current);
                    TransactionCode test;
                    test.align = origin;
                    if ((test.hash & current_hash) == current_hash) 
                    {//Bloom filter shows it is already there
                        return false; 
                    }

                    do{
                        test.align = origin;
                        if (test.flag & wr_c)
                        {
                            //some transaction owns write-lock
                            if (!is_exclusive_access(current))
                                throw ConcurentLockException();
                            //if this transaction already has write-lock, no need to obtain read
                            return false;
                        }
                        
                        test.transactions_count++;
                        test.hash ^= current_hash;
                    } while (do_compare_exchange(origin, test.align));
                    return true;
                }
                /**for multiple transactions read locks wipe-out current transaction
                * @return number of transactions that keeps lock
                */
                std::uint16_t leave(Transaction::transaction_id_t current)
                {
                    //@! to review: atomic XOR
                    std::uint64_t origin = _transaction_code.align;
                    auto current_hash = TransactionCode::transaction_id_to_hash(current);
                    TransactionCode test;
                    do{
                        test.align = origin;
                        if (test.flag & wr_c) //exclusive lock
                        {
                            return 0; //instead of decrement count just allow erase
                        }
                        
                        test.transactions_count--;
                        test.hash ^= current_hash;
                    } while (do_compare_exchange(origin, test.align));
                    return test.transactions_count;
                }
                std::uint8_t* shadow_buffer() const
                {
                    return _shadow_buffer;
                }
                TransactionCode transaction_code() const
                {
                    return _transaction_code;
                }
                
            private:
                bool do_compare_exchange(std::uint64_t& expected, std::uint64_t desired)
                {
                    //@! review migrate to atomic
                    if (_transaction_code.align == expected)
                    {
                        _transaction_code.align = desired;                        return true;                    }                    expected = _transaction_code.align;                    return false;
                }
                TransactionCode _transaction_code;
                std::uint8_t* _shadow_buffer;

            };
            typedef std::map<RWR, BlockUse> captured_blocks_t;
            struct TransactionImpl : public Transaction, std::enable_shared_from_this<TransactionImpl>
            {
                TransactionImpl(transaction_id_t id, TransactedSegmentManager *owner) :
                    Transaction(id),
                    _owner(owner),
                    _recursion_count(1)
                {

                }
                void recursive()
                {
                    ++_recursion_count;
                }
                void rollback() override
                {
                    if (!(--_recursion_count))
                    {
                        _owner->finalize_transaction(this->shared_from_this(), false);
                        _transaction_log.clear();
                    }
                }
                void commit() override
                {
                    if (!(--_recursion_count))
                    {
                        _owner->finalize_transaction(this->shared_from_this(), true);
                        _transaction_log.clear();
                    }
                }
                void store(captured_blocks_t::iterator& entry)
                {
                    if (entry->second.shadow_buffer())
                        on_shadow_buffer_created(entry->second.shadow_buffer(), entry->first.pos());
                    _transaction_log.emplace_back(entry);
                }
                /**Create mapping between pointer of shaddow buffer and real far-address*/
                void on_shadow_buffer_created(std::uint8_t*addr, far_pos_t pos)
                {
                    _address_lookup.emplace(addr, pos);
                }
                /**Lookup if far-pos exists for specified memory address of shadow buffer
                *@return true if address was found
                */
                bool get_address_by_shadow(const std::uint8_t*addr, far_pos_t& pos) const
                {
                    address_lookup_t::const_iterator found = _address_lookup.find(addr);
                    if (found == _address_lookup.end())
                    {
                        pos = found->second;
                        return true;
                    }
                    return false;
                }
                /**Since std::map::iterator is stable can store it*/
                typedef std::deque<captured_blocks_t::iterator> log_t;
                TransactedSegmentManager *_owner;
                log_t _transaction_log;
                typedef std::unordered_map<const std::uint8_t*, far_pos_t> address_lookup_t;
                address_lookup_t _address_lookup;
                std::atomic<int> _recursion_count;
            };
            typedef std::shared_ptr<TransactionImpl> transaction_impl_ptr_t;

            /**
            *   @param tr - transaction to process, the reason to use shared_ptr is allow erase from global transaction map
            *       without doubt about instance consistency
            */
            void finalize_transaction(transaction_impl_ptr_t& tr, bool is_commit)
            {
                //wipe this transaction on background
                auto erase_promise = std::async(std::launch::async, [this](std::thread::id id){
                    guard_t g1(&_opened_transactions_lock);
                    _opened_transactions.erase(id);
                }, std::this_thread::get_id());

                if (true)
                {
                    guard_t g2(&_map_lock); //shame! to improve need some lock-less approach
                    for (auto& i : tr->_transaction_log)
                    {
                        auto& block_pair = *i;
                        auto flag = block_pair.second.transaction_code().flag;
                        bool to_erase = false;
                        if ((flag & wr_c))
                        { //have to be exclusive 
                            if ( is_commit && !(flag & optimistic_write_c) ) //commit only non-optimistic write blocks
                            {
                                raw_write(block_pair.first.pos(), block_pair.second.shadow_buffer(), block_pair.first.count());
                            }
                            else if (!is_commit && (flag & optimistic_write_c))
                            { //rollback only optimistic-write blocks
                                raw_write(block_pair.first.pos(), block_pair.second.shadow_buffer(), block_pair.first.count());
                            }
                            // write-blocks uses exclusive access, must be removed from global 
                            to_erase = true;
                        }
                        else //only read permission
                        {
                            to_erase = 0 == block_pair.second.leave(tr->transaction_id());
                        }
                        if (to_erase) //erase only blocks that has no other owners
                            _captured_blocks.erase(i);
                    }
                }
                erase_promise.wait();
            }
            /**Handles delete of transaction. This functor intended for: 
            \li allow place *noexcept* to destructor of Transaction,
            \li to centralize handle of delete with access to this manager
            */
            struct TransactionDeleter
            {
                TransactionDeleter(TransactedSegmentManager* owner) :
                    _owner(owner){}
                void operator ()(Transaction * ptr)
                {
                    //need lower capture level from global map
                    delete ptr;
                }
                TransactedSegmentManager *_owner;
            };
            typedef std::unordered_map<std::thread::id, transaction_impl_ptr_t> opened_transactions_t;
            typedef FetchTicket<size_t> lock_t;
            typedef operation_guard_t< lock_t, &lock_t::lock, &lock_t::unlock> guard_t;

            lock_t _map_lock;
            captured_blocks_t _captured_blocks;
            std::atomic<Transaction::transaction_id_t> _transaction_uid_gen;
            
            lock_t _opened_transactions_lock;
            opened_transactions_t _opened_transactions;
            TransactionDeleter _transaction_deleter;
            unsigned _transaction_nesting;
            

        };
    }
} //end of namespace OP

#endif //_OP_VTM_TRANSACTEDSEGMENTMANAGER__H_
