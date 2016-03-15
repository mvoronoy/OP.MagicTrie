#ifndef _OP_VTM_TRANSACTEDSEGMENTMANAGER__H_
#define _OP_VTM_TRANSACTEDSEGMENTMANAGER__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/Transactional.h>
#include <op/common/Exceptions.h>
#include <unordered_map>
#include <memory>
#include <stack>
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
                guard_t g(_opened_transactions_lock);
                auto insres = _opened_transactions.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(std::this_thread::get_id()),
                    std::forward_as_tuple());
                if (insres.second) //just insert
                {
                    insres.first->second = transaction_impl_ptr_t(
                        new TransactionImpl(_transaction_uid_gen.fetch_add(1), this));
                    return insres.first->second;
                }
                //transaction already exists, just create save-point
                return insres.first->second->recursive();
                
            }
            
            ReadonlyMemoryChunk readonly_block(FarAddress pos, segment_pos_t size, ReadonlyBlockHint::type hint = ReadonlyBlockHint::ro_no_hint_c) override
            {
                transaction_impl_ptr_t current_transaction;
                captured_blocks_t::iterator found_res;
                ReadonlyMemoryChunk result{ do_readonly_block(pos, size, hint, current_transaction, found_res) };
                if (!current_transaction) //no current transaction, just return
                    return result;

                if ((hint & ReadonlyBlockHint::ro_keep_lock) == 0 //no keep-lock requirements
                   // && !found_res->second.is_exclusive_access(current_transaction->transaction_id())   //is not exclusive mode
                    )
                { //add disposer that releases lock at MemoryChunk destroy
                    result.emplace_disposable(std::make_unique<LockDisposer>(current_transaction, found_res));
                }
                return result;
            }
            MemoryChunk writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
            {
                RWR search_range(pos, size);
                guard_t g2(_map_lock); //trick - capture ownership in this thread, but delegate find in other one
                //@! need hard test to ensure this parallelism has a benefit in compare with _captured_blocks.emplace/.insert
                std::future<captured_blocks_t::iterator> real_mem_future = std::async(std::launch::async, [&](){ 
                    return _captured_blocks.lower_bound(search_range);
                });
                transaction_impl_ptr_t current;
                if (true)
                {  //extract current transaction in safe way
                    guard_t g1(_opened_transactions_lock);
                    auto found = _opened_transactions.find(std::this_thread::get_id());
                    if (found != _opened_transactions.end()) 
                    {
                        current = found->second;
                    }
                }
                if (!current)//no current transaction at all
                {
                    real_mem_future.wait();
                    throw Exception(er_transaction_not_started, "write allowed only inside transaction");
                }
                auto super_res = SegmentManager::writable_block(pos, size, hint);
                auto found_res = real_mem_future.get();
                if (found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first)))
                {//block already have associated transaction(s)
                    // don't allow transactions on overlapped memory blocks
                    if (!found_res->first.is_included(search_range))
                    {
                        if(WritableBlockHint::allow_block_realloc != (hint & WritableBlockHint::allow_block_realloc))
                            throw Exception(er_overlapping_block);
                        search_range = unite_range(search_range, found_res->first);
                        super_res = std::move(SegmentManager::writable_block(search_range.pos(), search_range.count(), hint));
                        // extend existing if allowed
                        extend_memory_block(found_res, search_range, super_res, current);
                    }
                    //only exclusive access is permitted
                    if (!found_res->second.is_exclusive_access(current->transaction_id()))
                        throw OP::vtm::ConcurentLockException();
                    //may be block just for read
                    if (!(found_res->second.transaction_code().flag & wr_c))
                    {//obtain write lock over already existing read-lock
                         
                        //clone memory block, so other could see old result
                        found_res->second.create_shadow(true, //this mean create buffer for read (optimistic write)
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
                    
                    //clone memory block, so other could see old result
                    found_res->second.create_shadow(false, //this mean create buffer for write (instead of buffer for read)
                        super_res.pos(),
                        super_res.count(),
                        hint
                        );
                    current->store(found_res);
                }
                //shadow buffer is returned only for pessimistic write, otherwise origin memory have to be used
                if (found_res->second.transaction_code().flag & optimistic_write_c)
                    return super_res;
                auto include_delta = pos - found_res->first.pos();//handle case when included region queried
                return MemoryChunk( found_res->second.shadow_buffer()+include_delta, size,
                        std::move(pos), std::move(SegmentManager::get_segment(pos.segment)) );
            }
            MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro)  override
            {
                return this->writable_block(ro.address(), ro.count());
            }
            
            far_pos_t to_far(segment_idx_t segment, const void * address) override
            {
                transaction_impl_ptr_t current;
                if (true)
                {  //extract current transaction in safe way
                    guard_t g1(_opened_transactions_lock);
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
                {
                    if (segment_of_far(result) != segment)
                        throw trie::Exception(trie::er_invalid_block);
                    return result;
                }
                //no shaddow buffer, return the origin
                return SegmentManager::to_far(segment, address);
            }

        protected:
            TransactedSegmentManager(const char * file_name, bool create_new, bool readonly) :
                SegmentManager(file_name, create_new, readonly)
            {
                _transaction_nesting = 0;
            }
            virtual std::shared_ptr<Transaction> get_current_transaction()
            {
                //this method can be replaced if compiler supports 'thread_local' keyword
                guard_t g(_opened_transactions_lock);
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
            /**Block usage flag*/
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
                enum
                {
                    off_flag = 16,
                    off_hash = 2+16
                };
                TransactionCode() 
                    : align(0){}
                TransactionCode(std::uint64_t aalign)
                    : align(aalign) {}
                TransactionCode(flag_t capture_kind, Transaction::transaction_id_t id) 
                    : transactions_count(1)
                    , flag(capture_kind)
                    , hash(transaction_id_to_hash(id))
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
                BlockUse(flag_t capture_kind, Transaction::transaction_id_t id) OP_NOEXCEPT
                    : _transaction_code(TransactionCode(capture_kind, id).align)
                    , _shadow_buffer(nullptr)
                {
                    
                }
                BlockUse(const BlockUse&) = delete;
                BlockUse& operator = (const BlockUse&other) = delete;

                BlockUse(BlockUse&&other) OP_NOEXCEPT
                    : _transaction_code(other._transaction_code.load())
                    , _shadow_buffer(other._shadow_buffer)
                {
                    other._shadow_buffer = nullptr;
                }
                BlockUse& operator = (BlockUse&&other) OP_NOEXCEPT
                {
                    _transaction_code = other._transaction_code.load();
                    _shadow_buffer = (other._shadow_buffer);
                    other._shadow_buffer = nullptr;
                }
                ~BlockUse()
                {
                    if (_shadow_buffer)
                        delete[]_shadow_buffer;
                }
                void create_shadow(bool is_optimistic_write, const std::uint8_t* from, segment_pos_t size,
                    WritableBlockHint hint = WritableBlockHint::block_no_hint_c)
                {
                    assert(_shadow_buffer == nullptr);
                    _shadow_buffer = new std::uint8_t[size];

                    if (WritableBlockHint::block_for_read_c == (hint & WritableBlockHint::block_for_read_c)) //write only don't need create a copy
                        memcpy(_shadow_buffer, from, size);
                    
                    if (is_optimistic_write)
                        _transaction_code|= static_cast<std::uint64_t>(optimistic_write_c) << TransactionCode::off_flag;
                    else
                        _transaction_code &= ~(static_cast<std::uint64_t>(optimistic_write_c)<< TransactionCode::off_flag);
                    _transaction_code |= static_cast<std::uint64_t>(wr_c) << TransactionCode::off_flag;
                }
                template <class MemBlock>
                void update_shadow(const MemBlock& new_mem, segment_pos_t old_size, segment_pos_t relative_offset = 0)
                {
                    if (!_shadow_buffer) //nothing to do
                        return;
                    assert((old_size + relative_offset) < new_mem.count()); //new block MUST be bigger than origin
                    auto new_buffer = new std::uint8_t[new_mem.count()];
                    memcpy(new_buffer, new_mem.pos(), new_mem.count()); //copy origin memory
                    memcpy(new_buffer + relative_offset, _shadow_buffer, old_size); //override with origin 
                    delete[]_shadow_buffer;
                    _shadow_buffer = new_buffer;
                }
                bool is_exclusive_access(Transaction::transaction_id_t current) const
                {
                    //use Bloom filter
                    //auto c = TransactionCode::transaction_id_to_hash(current);
                    TransactionCode test(wr_c, current);
                    TransactionCode origin(_transaction_code.load());
                    origin.flag &= ~optimistic_write_c; //ignore optimistic_write_c
                    if (origin.align == test.align)
                        return true;
                    //check if ro-access for 1 thread only
                    test.flag = ro_c;
                    return origin.align == test.align;
                }
                /**
                *   Mark this block as available for multiple read operations. 
                * If current transaction already has write 
                * access, nothing happens.
                * @return true if lock was accured, false mean that lock was already obtained before
                * @throws ConcurentLockException if other transaction owns write-lock
                */
                bool permit_read(Transaction::transaction_id_t current) 
                {
                    //@! to review: atomic XOR
                    std::uint64_t origin = _transaction_code.load();
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
                    std::uint64_t origin = _transaction_code.load();
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
                    return TransactionCode(_transaction_code.load());
                }
                
            private:
                
                bool do_compare_exchange(std::uint64_t& expected, std::uint64_t desired)
                {
                    return _transaction_code.compare_exchange_strong(expected, desired);
                }
                std::atomic<std::uint64_t> _transaction_code;
                std::uint8_t* _shadow_buffer;

            };
            typedef std::map<RWR, BlockUse> captured_blocks_t;
            struct TransactionImpl;
            struct SavePoint : public Transaction
            {
                /**Since std::map::iterator is stable can store it.*/
                typedef std::deque<captured_blocks_t::iterator> log_t;
                typedef log_t::iterator iterator;

                SavePoint(TransactionImpl* framed_tran)
                    : Transaction(framed_tran->transaction_id())
                    , _framed_tran(framed_tran)
                    {}
                void rollback() override
                {
                    _framed_tran->_owner->apply_transaction_log(transaction_id(), *this, false);
                    _transaction_log.clear();
                }
                iterator begin()
                {
                    return _transaction_log.begin();
                }
                iterator end()
                {
                    return _transaction_log.end();
                }
                void commit() override
                {
                    //do nothing
                }
                void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
                {
                    _framed_tran->register_handle(std::move(handler));
                }
                void store(captured_blocks_t::iterator& pos)
                {
                    //Note this queue organized in reverse order - in front goes last
                    _transaction_log.emplace_front(pos);
                }
                /**When RO block no more used it can be released (on condition no ReadonlyBlockHint::ro_keep_lock set) 
                *   @return true if specific entry was removed from save-point
                */
                bool remove_block(const captured_blocks_t::iterator& entry)
                {
                    auto &key = entry->first;
                    //the probability to meet correct entry in deque raises when scanning in reverse direction

                    for (auto ri = _transaction_log.begin(); ri != _transaction_log.end(); ++ri)
                    {
                        auto& current = (**ri);
                        //to compare use property of map's iterator stability
                        //the pointer to the key is never changed during a time
                        if (&current.first == &key) //compare pointers only
                        {
                            _transaction_log.erase(ri);
                            
                            //it mustn't be possible that block owns shadow memory
                            assert(current.second.shadow_buffer() == nullptr);
                            return true;
                        }
                    }
                    return false;
                }
            private:
                TransactionImpl* _framed_tran;
                log_t _transaction_log;
            };
            /**Implement Transaction interface*/
            struct TransactionImpl : public Transaction, std::enable_shared_from_this<TransactionImpl>
            {
                TransactionImpl(transaction_id_t id, TransactedSegmentManager *owner)
                    : Transaction(id)
                    , _owner(owner)
                {
                    _save_points.emplace_back(new SavePoint(this));
                }
                /**Client code may claim nested transaction. Instead of real transaction just provide save-point
                * @return new instance of SavePoint with the same transaction-id as current one
                */
                transaction_ptr_t recursive()
                {
                    _save_points.emplace_back(new SavePoint(this));
                    return _save_points.back();
                }
                virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler)
                {
                    _end_listener.emplace_back(std::move(handler));
                }
                void rollback() override
                {
                    finish_transaction(false);
                }
                void commit() override
                {
                    finish_transaction(true);
                }
                void store(captured_blocks_t::iterator& entry)
                {
                    if (entry->second.shadow_buffer())
                        on_shadow_buffer_created(entry->second.shadow_buffer(), entry->first.pos());
                    _save_points.back()->store(entry);
                }
                /**Allows discover state if transaction still exists (not in ghost state) */
                bool is_active() const
                {
                    return !_save_points.empty();
                }
                /**Create mapping between pointer of shaddow buffer and real far-address*/
                void on_shadow_buffer_created(const std::uint8_t*addr, far_pos_t pos)
                {
                    _address_lookup.emplace(addr, pos);
                }
                /**Remove existing mapping between shaddow buffer.
                @return true if address existed
                */
                bool erase_shadow_buffer_created(const std::uint8_t*addr)
                {
                    return _address_lookup.erase(addr) > 0;
                }
                /**Lookup if far-pos exists for specified memory address of shadow buffer
                *@return true if address was found
                */
                bool get_address_by_shadow(const std::uint8_t*addr, far_pos_t& pos) const
                {
                    address_lookup_t::const_iterator found = _address_lookup.find(addr);
                    if (found != _address_lookup.end())
                    {
                        pos = found->second;
                        return true;
                    }
                    return false;
                }
                /**Remove some entry from transaction log*/
                void erase(const captured_blocks_t::iterator& entry)
                {
                    //the probability to meet correct entry in deque raises when scanning in reverse direction
                    for (auto ri = _save_points.rbegin(); ri != _save_points.rend(); ++ri)
                    {
                        auto& save_point = *ri;
                        if (save_point->remove_block(entry))
                        { //stop iteration
                            return;
                        }
                    }
                }
                /**Perform commit or rollback operation*/
                void finish_transaction(bool is_commit)
                {
                    assert(!_save_points.empty());
                    //invoke events on transaction end
                    auto event_callback = is_commit ? &BeforeTransactionEnd::on_commit : &BeforeTransactionEnd::on_rollback;
                    for (auto& ev : _end_listener)
                    {
                        ((*ev).*event_callback)();
                    }
                    //wipe this transaction on background
                    auto erase_promise = std::async(std::launch::async, [this](std::thread::id id){
                        _owner->erase_transaction(id);
                    }, std::this_thread::get_id());
                    //iterate all save-points and apply commit or rollback to each one
                    for (auto ri = _save_points.rbegin(); ri != _save_points.rend(); ++ri)
                    {
                        auto& slog = *ri;
                        _owner->apply_transaction_log(transaction_id(), *slog, is_commit);
                    }
                    _save_points.clear();
                    //wait untill transaction is erased
                    erase_promise.wait();
                }
                TransactedSegmentManager *_owner;

                typedef std::unordered_map<const std::uint8_t*, far_pos_t> address_lookup_t;
                address_lookup_t _address_lookup;
                typedef std::deque<std::shared_ptr<SavePoint>> save_point_stack_t;
                save_point_stack_t _save_points;
                typedef std::vector<std::unique_ptr<BeforeTransactionEnd>> transaction_end_listener_t;
                transaction_end_listener_t _end_listener;
            };
            typedef std::shared_ptr<TransactionImpl> transaction_impl_ptr_t;
            struct LockDisposer : public BlockDisposer
            {
                LockDisposer(transaction_impl_ptr_t& owner, captured_blocks_t::iterator& entry)
                    : _owner(owner)
                    , _entry(entry)
                {}
                void on_leave_scope(MemoryChunkBase& closing) OP_NOEXCEPT
                {
                    if (_owner->is_active()) //process only on active transactions (that have some open blocks)
                    {
                        _owner->_owner->release_ro_lock(_owner, _entry);
                    }
                }
            private:
                transaction_impl_ptr_t _owner;
                captured_blocks_t::iterator _entry;
            };
            template <class SavePoint>
            void apply_transaction_log(Transaction::transaction_id_t tid, SavePoint& save_points, bool is_commit)
            {
                guard_t g2(_map_lock); //shame @! to improve need some lock-less approach
                auto end = save_points.end();
                for (auto begin = save_points.begin(); begin != end; ++begin)
                {
                    auto& block_pair = **begin;
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
                        to_erase = 0 == block_pair.second.leave(tid);
                    }
                    if (to_erase) //erase only blocks that has no other owners
                        _captured_blocks.erase(*begin);
                }
            }
            /**Allows extend memory block in case of overlapped exception
            * @param to_extend - iterator in `_captured_blocks` with overlapped block, at exit this parameter contains valid
            *                    position of just recreate item   
            * @throws Exception with code `er_overlapping_block` if extnsion of block 'touches' neighbour blocks from
            *                   `_captured_blocks` map
            */
            template <class Iterator, class MemBlock>
            void extend_memory_block(Iterator& to_extend, RWR &new_range, MemBlock& real_mem, transaction_impl_ptr_t& current_tran)
            {
                auto before = to_extend;
                --before;
                if (before != _captured_blocks.end() //not the first item
                    && !join_range(before->first, new_range).empty() //check result range has no commons with previous block
                    )
                {
                    throw Exception(er_overlapping_block);
                }
                auto after = to_extend;
                ++after;
                if(after != _captured_blocks.end() //not the last item
                    && !join_range(after->first, new_range).empty() //check result range has no commons with next block
                    )
                {
                    throw Exception(er_overlapping_block);
                }
                //erase old and append to map brand-new key instead
                auto new_val = std::move(to_extend->second);
                assert(current_tran->erase_shadow_buffer_created(new_val.shadow_buffer()));
                //if shadow buffer exists update it to bigger 
                new_val.update_shadow(real_mem, to_extend->first.count(),
                    static_cast<segment_pos_t>(to_extend->first.pos() - new_range.pos()));
                current_tran->on_shadow_buffer_created(new_val.shadow_buffer(), real_mem.address());
                auto erased = _captured_blocks.erase(to_extend);
                to_extend = _captured_blocks.emplace_hint(erased, //use erasure pos as ahint  
                    RWR(new_range.pos(), new_range.count()),
                    std::move(new_val));
            }
            /**
            * wipe the transaction  
            * @param tid - id of thread that owns by transaction
            */
            void erase_transaction(std::thread::id tid)
            {
                guard_t g1(_opened_transactions_lock);
                _opened_transactions.erase(tid);
            }
            /**remove RO lock for single block*/
            void release_ro_lock(transaction_impl_ptr_t& tr, captured_blocks_t::iterator& entry)
            {
                guard_t g2(_map_lock); 
                //some blocks could upgraded to write
                auto flag = entry->second.transaction_code().flag;
                if (flag & wr_c)//write lock must not be released
                    return;
                //wipe out block from transaction
                tr->erase(entry);
                //may be other transactions retain this region
                if (0 == entry->second.leave(tr->transaction_id()))
                {
                    _captured_blocks.erase(entry);
                }
            }
            /**
            * @param current_transaction - output paramter, may be nullptr at exit if no current transaction started
            * @param found_res - output paramter, iterator to map of captures
            */
            ReadonlyMemoryChunk do_readonly_block (
                FarAddress pos, segment_pos_t size, ReadonlyBlockHint::type hint, 
                transaction_impl_ptr_t& current_transaction,
                captured_blocks_t::iterator& found_res) 
            {
                RWR search_range(pos, size);
                guard_t g2(_map_lock); //trick - capture ownership in this thread, but delegate find in other one
                //@! need hard test to ensure this parallelism has a benefit in compare with _captured_blocks.emplace/.insert
                std::future<captured_blocks_t::iterator> real_mem_future = std::async(std::launch::async, [&](){ 
                    return _captured_blocks.lower_bound(search_range);
                });

                if (true)
                {  //extract current transaction in safe way
                    guard_t g1(_opened_transactions_lock);
                    auto found = _opened_transactions.find(std::this_thread::get_id());
                    if (found != _opened_transactions.end()) 
                    {
                        current_transaction = found->second;
                    }
                }
                if (!current_transaction) //no current transaction at all
                {
                    //check presence of queried range in shadow buffers
                    found_res = real_mem_future.get();
                    if(found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first))) 
                    {
                        // range exists. 
                        if (!found_res->first.is_included(search_range))// don't allow trnsactions on overlapped memory blocks
                            throw Exception(er_overlapping_block);
                        if (found_res->second.transaction_code().flag & wr_c) //some write-tran already captured this block
                            throw ConcurentLockException();
                        if (found_res->second.shadow_buffer() != nullptr)
                        {
                            /* It is impossible to have shaddow-buffer and no wr-lock simultaniusly
                            auto dif_off = pos.diff(found_res->first.pos());
                            return ReadonlyMemoryChunk(
                                found_res->second.shadow_buffer() + dif_off,
                                size,
                                std::move(pos),
                                std::move(SegmentManager::get_segment(pos.segment))
                            );
                            */
                            assert(false);
                        }
                        //no shadow buffer, hence block used for RO only
                    }
                    //no shadow buffer for this region, just get RO memory
                    //@! review create snapshot buffer instead of raw memory access - since 'no transaction' can be dangerous
                    return SegmentManager::readonly_block(pos, size, hint);
                }
                
                found_res = real_mem_future.get();
                if (found_res != _captured_blocks.end() && !(_captured_blocks.key_comp()(search_range, found_res->first)))
                {//result found
                    if (!found_res->first.is_included(search_range))// don't allow transactions on overlapped memory blocks
                        throw Exception(er_overlapping_block);
                    //add hashcode to scope of transactions, may rise ConcurentLockException
                    if (found_res->second.permit_read(current_transaction->transaction_id()))
                    {//new lock was obtained, need persist to the transaction scope
                        current_transaction->store(found_res);
                    }
                }
                else//new entry, so no conflicts, place entry & return result
                {
                    found_res = _captured_blocks.emplace_hint(
                        found_res, //use previous found pos as a hint 
                        std::piecewise_construct, 
                        std::forward_as_tuple(search_range),
                        std::forward_as_tuple(ro_c, current_transaction->transaction_id())
                    );
                    current_transaction->store(found_res);
                }
                //Following 'if' checks only shadow-buffer without transaction_code().flag & optimistic_write_c
                //because if write=access accured the same transaction must always see changes
                //other transaction should fail on obtaining lock, so shadow-buffer is enough to check
                if ( found_res->second.shadow_buffer() == nullptr //no shadow buffer for this region, just get RO memory
                    )
                    return SegmentManager::readonly_block(pos, size, hint);
                //there since shadow presented. This possible only for already captured
                //write-lock, hence this read-only query belong to the same transaction
                //so need analyze what to return - either real buffer (optimistic write)
                //or backed shadow buffer (pessimistic write)
                if (found_res->second.transaction_code().flag & optimistic_write_c)
                    return SegmentManager::readonly_block(pos, size, hint);
                auto include_delta = pos - found_res->first.pos();//handle case when included region queried
                return ReadonlyMemoryChunk(found_res->second.shadow_buffer()+include_delta, //use shadow
                    size, std::move(pos), std::move(SegmentManager::get_segment(pos.segment)));
            }
            typedef std::unordered_map<std::thread::id, transaction_impl_ptr_t> opened_transactions_t;
            /*typedef FetchTicket<size_t> lock_t;
            typedef operation_guard_t< lock_t, &lock_t::lock, &lock_t::unlock> guard_t;
            */
            typedef std::recursive_mutex lock_t;
            typedef std::unique_lock<typename lock_t> guard_t;

            lock_t _map_lock;
            captured_blocks_t _captured_blocks;
            std::atomic<Transaction::transaction_id_t> _transaction_uid_gen;
            
            mutable lock_t _opened_transactions_lock;
            opened_transactions_t _opened_transactions;
            unsigned _transaction_nesting;
            
        };
    }
} //end of namespace OP

#endif //_OP_VTM_TRANSACTEDSEGMENTMANAGER__H_
