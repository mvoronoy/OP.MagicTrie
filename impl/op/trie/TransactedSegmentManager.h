#pragma once
#include <op/trie/SegmentManager.h>
#include <op/trie/Transactional.h>
#include <op/trie/Exceptions.h>
#include <unordered_map>
#include <memory>
#include <deque>
#include <thread>

namespace OP
{
    namespace trie{
        class TransactedSegmentManager;
        
        struct Transaction
        {
            typedef std::uint64_t transaction_id_t;
            friend class TransactedSegmentManager;

            Transaction() = delete;
            Transaction(const Transaction& ) = delete;
            Transaction& operator = (const Transaction& ) = delete;
            Transaction& operator = (Transaction&& other) = delete;

            Transaction(Transaction && other) OP_NOEXCEPT :
                _transaction_id(other._transaction_id),
                _transaction_log(other._transaction_log)
            {

            }
            ~Transaction() OP_NOEXCEPT
            {
                
            }
            transaction_id_t transaction_id() const
            {
                return _transaction_id;
            }
            void rollback()
            {

            }
            void commit()
            {

            }
        protected:
            Transaction(transaction_id_t id) :
                _transaction_id(id)
            {

            }
        private:                
            typedef std::deque</*captured_blocks_t::iterator*/Transactable*> log_t;
            const transaction_id_t _transaction_id;
            log_t _transaction_log;
        };

        class TransactedSegmentManager :
            public SegmentManager
        {
            friend struct SegmentManager;
            friend struct Transaction;
        public:
            typedef std::shared_ptr<Transaction> transaction_ptr_t;
            ~TransactedSegmentManager()
            {
            }
            transaction_ptr_t begin_transaction()
            {
                guard_t g(&_opened_transactions_lock);
                auto insres = _opened_transactions.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(std::this_thread::get_id()),
                    std::forward_as_tuple());
                if (insres.second) //just insert
                {
                    insres.first->second = transaction_ptr_t(new Transaction(
                        _transaction_uid_gen.fetch_add(1)
                        ), _transaction_deleter);
                }
                return insres.first->second;
            }
            
            ReadonlyMemoryRange readonly_block(FarPosHolder pos, segment_pos_t size) override
            {
                RWR search_range(pos, size);
                guard_t g2(&_map_lock); //trick - capture ownership in this thread, but delegate find in other one
                //@! need hard test to ensure this parallelism has a benefit in compare with _captured_blocks.emplace/.insert
                std::future<captured_blocks_t::iterator> real_mem_future = std::async(std::launch::async, [&](){ 
                    return _captured_blocks.lower_bound(search_range);
                });
                transaction_ptr_t current;
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
                        if (found_res->second.shadow_buffer() != nullptr)
                        {
                            auto dif_off = pos.diff(found_res->first.pos());
                            return ReadonlyMemoryRange(
                                found_res->second.shadow_buffer() + dif_off,
                                size,
                                std::move(pos),
                                std::move(SegmentManager::get_segment(pos.segment()))
                            );
                        }
                        //no shadow buffer, hence block used for RO only
                    }
                    //no shadow buffer for this region, just get RO memory
                    return SegmentManager::readonly_block(pos, size);
                }
                //RWR enty(result);
                auto found_res = real_mem_future.get();
                if (found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first)))
                {//result found
                    if (!found_res->first.is_included(search_range))// don't allow trnsactions on overlapped memory blocks
                        throw Exception(er_overlapping_block);
                    //add hashcode to scope of transactions, may rise ConcurentLockException
                    found_res->second.permit_read(current->transaction_id());

                }
                else//new entry, so no conflicts, just return result
                {
                    _captured_blocks.emplace_hint(
                        found_res, //use previous found pos as ahint 
                        std::piecewise_construct, 
                        std::forward_as_tuple(search_range),
                        std::forward_as_tuple(ro_c, current->transaction_id())
                    );
                    return super_res;
                }
                auto ins_res = _captured_blocks.emplace( 
                    std::piecewise_construct,
                    std::forward_as_tuple(pos, size),
                    
                );
                if (ins_res.second) //new entry, so no conflicts, just return result
                {
                    return super_res;
                }
               

                return ReadonlyMemoryRange(ins_res.first->second.shadow_buffer() == nullptr ?
                    ins_res.first->first.pos() //use real memory
                    : ins_res.first->second.shadow_buffer(), //use shadow
                    size, std::move(pos), std::move(super_res.segment()));
            }
            MemoryRange writable_block(FarPosHolder pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
            {
                //@! need hard test to ensure this parallelism has a benefit
                std::future<MemoryRange> real_mem_future = std::async(
                    std::launch::async, 
                    [this, pos, size, hint](){ 
                        return SegmentManager::writable_block(pos, size, hint); 
                    }
                );
                transaction_ptr_t current;
                if (true)
                {
                    guard_t g1(&_opened_transactions_lock);
                    auto found = _opened_transactions.find(std::this_thread::get_id());
                    if (found == _opened_transactions.end()) //no current transaction at all
                        throw Exception(er_transaction_not_started, "write allowed only inside transaction");
                    current = found->second;
                }
                auto super_res = real_mem_future.get();
                //try insert block
                guard_t g2(&_map_lock);
                std::pair<captured_blocks_t::iterator, bool> ins_res = _captured_blocks.emplace( 
                    std::piecewise_construct,
                    std::forward_as_tuple(super_res),
                    std::forward_as_tuple(wr_c, current->transaction_id())
                );
                if (ins_res.second) //new entry, so no conflicts, just return result
                {
                    //clone memory block, so other could see old result
                    ins_res.first->second.create_shaddow(false, //this mean create buffer for write (instead of buffer for read)
                        super_res.pos(),
                        super_res.count(),
                        hint
                        );
                }
                else //block already have associated transaction(s)
                {
                    // don't allow transactions on overlapped memory blocks
                    if (!ins_res.first->first.is_included(super_res))
                        throw Exception(er_overlapping_block);
                    //only exclusive access is permitted
                    if (!ins_res.first->second.is_exclusive_access(current->transaction_id()))
                        throw ConcurentLockException();
                }
                return MemoryRange( ins_res.first->second.shadow_buffer(), size,
                        std::move(pos), std::move(super_res.segment()) );
            }
            MemoryRange upgrade_to_writable_block(ReadonlyMemoryRange& ro)  override
            {
                return SegmentManager::upgrade_to_writable_block(ro);
            }

        protected:
            TransactedSegmentManager(const char * file_name, bool create_new, bool readonly) :
                SegmentManager(file_name, create_new, readonly),
                _transaction_deleter(this)
            {
                _transaction_nesting = 0;
            }
            void begin_write_operation() override
            {
                begin_transaction();
            }
            void end_write_operation() override
            {
            }
            virtual std::shared_ptr<Transaction> get_current_transaction()
            {
                //this method can be replaced if compiler supports 'thread_local' keyword
                guard_t g(&_opened_transactions_lock);
                auto found = _opened_transactions.find(std::this_thread::get_id());
                return found == _opened_transactions.end() ? std::shared_ptr<Transaction>() : found->second;
            }
            void do_commit(Transaction * tr)
            {
                tr->_transaction_log
                //wipe this transaction on background
                std::async(std::launch::async, [this](std::thread::id id){
                    guard_t g1(&_opened_transactions_lock);
                    _opened_transactions.erase(id);
                }, std::this_thread::get_id());
            }
        private:
            typedef OP::Range<FarPosHolder, segment_pos_t> RWR;
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
            struct BlockUse : public Transactable
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
                    return _transaction_code.align == test.align;
                }
                /**Mark this block as available for multiple read operations. 
                * If current transaction already has write 
                * access, nothing happens.
                * @return true if lock was accured, false mean that no lock is needed
                * @throws ConcurentLockException if other transaction owns write-lock
                */
                bool permit_read(Transaction::transaction_id_t current) 
                {
                    //@! to review: atomic XOR
                    std::uint64_t origin = _transaction_code.align;
                    auto current_hash = TransactionCode::transaction_id_to_hash(current);
                    TransactionCode test;
                    test.align = origin;
                    if ((test.hash & current_hash) == current_hash) //Bloom filter shows it is already there
                    {
                        return false; 
                    }

                    do{
                        test.align = origin;
                        if (test.flag & wr_c)
                        {
                            //other transaction owns write-lock
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
                std::uint8_t* shadow_buffer() const
                {
                    return _shadow_buffer;
                }
                /**Support Transactable interface, save result if modified*/
                void on_commit(TransactionMedia& media) override
                {
                    if ((_transaction_code.flag & wr_c) && !(_transaction_code.flag & optimistic_write_c))
                    {
                        media.write()
                    }
                }
                void on_rollback(TransactionMedia& media) = 0;

            private:
                bool do_compare_exchange(std::uint64_t& expected, std::uint64_t desired)
                {
                    //@! review migrate to atomic
                    if (_transaction_code.align == expected)
                    {
                        _transaction_code.align = desired;                        return false;                    }                    expected = _transaction_code.align;                    return false;
                }
                TransactionCode _transaction_code;
                std::uint8_t* _shadow_buffer;

            };
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
            typedef std::map<RWR, BlockUse> captured_blocks_t;
            typedef std::unordered_map<std::thread::id, std::shared_ptr<Transaction>> opened_transactions_t;
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
