#pragma once

#ifndef _OP_TRIE_SERVANTS_TRIEALLOCATORIMPL__H_ 
#define _OP_TRIE_SERVANTS_TRIEALLOCATORIMPL__H_

#include <mutex>
#include <array>
#include <unordered_map>

#include <op/common/ThreadPool.h>
#include <op/common/DependencyManager.h>

#include <op/flur/flur.h>

#include <op/vtm/datastructure/SizeRadixRoutingIndex.h>

#include <op/vtm/AppendOnlyMemoryPool.h>
#include <op/vtm/Transactional.h>

#include <op/trie/TrieOptions.h>
#include <op/trie/servants/SeviceUID.h>

namespace OP::trie::servants
{
    namespace opu = OP::utils;
    namespace opc = OP::common;

    namespace details
    {
        inline opu::ThreadPool make_thread_pool_if_not_specified()
        {
            return opu::ThreadPool(2);
        }
    }//ns:details
    
    enum class TrieEvents
    {
        /** Number of nodes has been increased.
        * Arguments: 
        * const Trie::key_t& - prefix string of new node.
        */
        node_created = 1,
        /** Number of nodes going to decrease 
        * Arguments:
        * const Trie::key_t& - prefix string of deleting node.
        */
        before_node_delete
    };


    class TrieAllocatorServiceImpl 
        : public TrieAllocatorService
        , public opc::DependencyManager< 
            opc::expecting<opu::ThreadPool>(), 
            opc::expecting<vtm::AppendOnlyMemoryPool>(),
            opc::expecting<vtm::SegmentManager>(),
            opc::expecting<trie::TrieTransactionService>(),
            opc::expecting<trie::TrieResidenceService>(),
            opc::expecting<trie::OpenMode>(),
            opc::expecting<trie::Logging>()//,
            //opc::or_default_factory(details::make_thread_pool_if_not_specified) 
          >
    {
        struct FreeBlock
        {
            vtm::FarAddress _next{};
        };

        struct BlockHeader
        {
            constexpr static inline std::uint16_t signature_c = ('B' << 8) | 'h';
            const std::uint16_t _signature = signature_c;
            std::uint16_t _size;

            constexpr explicit BlockHeader(std::uint16_t size) noexcept
                :_size(size)
            {}

            inline constexpr bool is_valid() const noexcept
            {
                return _signature == signature_c;
            }
        };

        constexpr static inline segment_pos_t platform_align_c = vtm::SegmentDef::align_c; //alignof(BlockHeader);
        constexpr static inline segment_pos_t header_size_c = OP::utils::aligned_sizeof<BlockHeader>(platform_align_c);

    public:
        constexpr static inline segment_pos_t min_block_size_c = OP::utils::aligned_sizeof<FreeBlock>(platform_align_c);
        
        ~TrieAllocatorServiceImpl()
        {
            std::unique_lock guard(_background_task_acc);
            if (_background_task.valid())
                _background_task.get();
        }

        virtual vtm::FarAddress allocate(segment_pos_t expected_size, AllocHint hint) override
        {
            expected_size = OP::utils::align_on(expected_size, platform_align_c);
            assert(expected_size >= min_block_size_c);

            const segment_pos_t real_size = header_size_c + expected_size;

            const segment_pos_t key = expected_size / platform_align_c;

            vtm::FarAddress result = reuse(key, hint);

            if(result.is_nil())
            {
                auto heuristic = heuristic_pre_alloc(real_size, hint);
                result = resolve<vtm::AppendOnlyMemoryPool>().allocate(heuristic * real_size);

                if( heuristic > 1 )
                { //allocated more than needed, so deposit items for future use
                    deposit(result + real_size, heuristic - 1, real_size);
                }
                //put header to the new block
                result = create_new_block_header(result, key);
            }
            //in transaction scope need store address to avoid memory leaks on rollback
            auto& ts = resolve<OP::trie::TrieTransactionService>();
            std::optional<transaction_id_t> tid = ts.current_transaction_id();
            if(tid.has_value())
            {
                std::unique_lock dealloc_guard(_to_complete_at_transaction_end_acc);
                auto [ins_pos, success] = _to_complete_at_transaction_end.try_emplace(*tid, std::in_place);
                ins_pos->second._allocated.emplace_back(result); //register just allocated address
            }
            return result;
        }

        virtual void deallocate(vtm::FarAddress address) noexcept override
        {

            auto& sm = resolve<OP::vtm::SegmentManager>();
            auto& ts = resolve<OP::trie::TrieTransactionService>();
            std::optional<transaction_id_t> tid = ts.current_transaction_id();

            if(tid.has_value())
            { // in transaction postpone real dealloc until commit
                std::unique_lock dealloc_guard(_to_complete_at_transaction_end_acc);
                auto [ins_pos, success] = _to_complete_at_transaction_end.try_emplace(*tid, std::in_place);
                ins_pos->second._deallocated.emplace_back(address); //push to vector to deallocate in future
            }
            else // no transaction (it can be kind of service code), need dealloc right now
            {
                free_single_block(address);
            }
        }

        virtual void deallocate(std::vector<vtm::FarAddress> to_remove)
        {
            auto& ts = resolve<OP::trie::TrieTransactionService>();
            std::optional<transaction_id_t> tid = ts.current_transaction_id();

            if(tid.has_value())
            { // in transaction postpone real dealloc until commit
                std::unique_lock dealloc_guard(_to_complete_at_transaction_end_acc);
                auto [ins_pos, success] = _to_complete_at_transaction_end.try_emplace(*tid, std::in_place);
                for(auto addr: to_remove)
                {
                    ins_pos->second._deallocated.emplace_back(addr); //push to vector
                }
            }
            else // no transaction (it can be kind of service code), can dealloc right now
            {
                for(const auto& address: to_remove)
                    free_single_block(address);
            }
        }


    protected:
        virtual void on_complete() override
        {
            auto& residence = resolve<trie::TrieResidenceService>();
            if (OpenMode::open == resolve<OpenMode>())
            {
                _size_index.construct(
                    resolve<OP::vtm::SegmentManager>(),
                    resolve<OP::vtm::AppendOnlyMemoryPool>(),
                    residence.residence(ServiceUID::alloc_service)
                );
            }
            else
            {
                _size_index.construct(
                    resolve<OP::vtm::SegmentManager>(),
                    resolve<OP::vtm::AppendOnlyMemoryPool>()
                );
                residence.store_residence(
                    _size_index->root_address(), ServiceUID::alloc_service);
            }

            auto& transaction_service = resolve<TrieTransactionService>();
            _tran_unsub = {
                transaction_service.event_supplier().on<vtm::TransactionEvent::committed>(
                    [this](transaction_id_t id){ on_commit(id); }),
                transaction_service.event_supplier().on<vtm::TransactionEvent::rollback>(
                    [this](transaction_id_t id){ on_rollback(id); })
            };
        }

    private:
        constexpr static inline segment_pos_t max_regular_size_c = 1024;
        constexpr static inline segment_pos_t max_string_size_c = 16 * 1024;

        using size_index_t = OP::vtm::SizeRadixRoutingIndex<FreeBlock, max_string_size_c>;
        using unsubscriber_t = typename trie::TrieTransactionService::event_supplier_t::unsubscriber_t;
        using transaction_id_t = typename vtm::TransactionEvent::transaction_id_t;


        OP::MemBuf<size_index_t> _size_index;
        std::future<void> _background_task;
        std::mutex _background_task_acc;
        std::array<unsubscriber_t, 2> _tran_unsub; //automatic unsubscribe from events

        struct TransactionAllocOperations
        {
            TransactionAllocOperations(std::in_place_t) {};
            std::vector<vtm::FarAddress> _allocated, _deallocated;
        };
        std::unordered_map<transaction_id_t, TransactionAllocOperations> _to_complete_at_transaction_end;
        std::mutex _to_complete_at_transaction_end_acc;

        std::optional<std::exception> _exception_trap;
        std::mutex _exception_trap_acc;


        /** Tries to locate expected memory block in the already freed pool.
         *  @param compressed_size - is not a block size, but `expected / platform_align_c`
         */
        FarAddress reuse(std::uint16_t compressed_size, AllocHint& hint)
        {
            auto claim_result = _size_index->claim(compressed_size, hint != AllocHint::irregular ); //allow bigger block only for irregulars
            return claim_result._block_address;
        }

        /** Heuristic based on AllocHint and block size to estimate how many items should be allocated in 
        *   the single query alongside with main request. 
        */
        unsigned heuristic_pre_alloc(segment_pos_t size, AllocHint hint) const noexcept
        {
            unsigned result = 1;

            if(hint == AllocHint::irregular)
                return size < 64 ? 8 : 1;
            return 16;
        }

        /** Put header to the just created block.
        * @param raw_addr - just allocated block. Must be big enough to fit user's expected `memory + header_size_c`.
        * @param compressed_size - compressed user's expected memory : `expected / platform_align_c`
        * @return correct address that should be used for user purpose (it is `raw_addr + header_size_c`).
        */ 
        FarAddress create_new_block_header(FarAddress raw_addr, std::uint16_t compressed_size)
        {
            auto& sm = resolve<OP::vtm::SegmentManager>();
            sm.accessor<BlockHeader>(raw_addr, vtm::WritableBlockHint::new_c).make_new(compressed_size);
            return raw_addr + header_size_c;
        }

        /** Mark block(s) as free */
        void deposit(FarAddress block, unsigned items_number, segment_pos_t item_real_size)
        {
            check_trapped_error();

            auto& threads = resolve<OP::utils::ThreadPool>();

            std::unique_lock guard(_background_task_acc);
            _background_task = threads.async(
                [this](FarAddress block, unsigned items_number, segment_pos_t item_real_size, std::future<void>&& previous){
                    try
                    {
                        using namespace OP::flur;
                        const segment_pos_t user_block_size = item_real_size - header_size_c;
                        const std::uint16_t compressed_size = static_cast<std::uint16_t>(user_block_size / platform_align_c);
                        
                        auto end_point = block + items_number * item_real_size;
                        // no need additional thread safety since deposit works on brand-new memory, other threads have no clue
                        auto chunk_maker = 
                            //incrementally go by all FarAddress in range
                            src::of_iota(block, end_point, item_real_size) 
                            //for each chunk put BlockHeader and eval where user address starts
                            >> then::mapping([&](FarAddress raw_addr) -> FarAddress{
                                return create_new_block_header(raw_addr, compressed_size);
                            });

                        auto result = (*_size_index).add_multiple_blocks(compressed_size, chunk_maker);
                        assert(result == size_index_t::AddStatus::ok);
                        std::ignore = result;

                        if(previous.valid()) //chain previous results
                            previous.get();
                    }
                    catch(const std::exception& ex)
                    {
                        trap_error(ex);
                    }
                }, block, items_number, item_real_size, std::move(_background_task));
        }

        /** Takes address of user space, gets to the BlockHeader and deposits entire block to _size_index */
        void free_single_block(vtm::FarAddress user_space) noexcept
        {
            FarAddress real_block_addr = user_space + (-static_cast<vtm::segment_off_t>(header_size_c));
            auto& sm = resolve<vtm::SegmentManager>();
            auto ro_block = sm.view<BlockHeader>(real_block_addr);
            if(!ro_block->is_valid())
            { //corruption of heap
                auto& logging = resolve<trie::Logging>();
                const char* message = "TrieAllocatorImpl heap corruption!";
                logging.log(logging.fatal, message);
                assert(false); //for debug purpose only
                std::terminate(); //causes immediate std::terminate as method is `noexcept`
            }
            auto result = 
                _size_index->add_block( user_space, ro_block->_size );
            assert(result == size_index_t::AddStatus::ok);
            std::ignore = result;
        }

        /** When transaction committed real block de-allocation is allowed. Do it on background thread */
        void on_commit(transaction_id_t id)
        {
            check_trapped_error();

            std::vector<FarAddress> ready_to_delete;
            std::unique_lock dealloc_guard(_to_complete_at_transaction_end_acc);
            if(auto found = _to_complete_at_transaction_end.find( id ); 
                found != _to_complete_at_transaction_end.end())
            {
                ready_to_delete = std::move(found->second._deallocated);
                _to_complete_at_transaction_end.erase(found);
            }
            dealloc_guard.unlock();
            gc_multiple_addresses(std::move(ready_to_delete));
        }
        
        void on_rollback(transaction_id_t id)
        {
            std::vector<FarAddress> ready_to_delete;

            std::unique_lock dealloc_guard(_to_complete_at_transaction_end_acc);
            //clear all with transaction id
            if (auto found = _to_complete_at_transaction_end.find(id);
                found != _to_complete_at_transaction_end.end())
            {
                ready_to_delete = std::move(found->second._allocated);
                _to_complete_at_transaction_end.erase(found);
            }
            dealloc_guard.unlock();
            gc_multiple_addresses(std::move(ready_to_delete));
        }

        void gc_multiple_addresses(std::vector<vtm::FarAddress> ready_to_delete)
        {
            if (!ready_to_delete.empty())
            {
                auto& threads = resolve<OP::utils::ThreadPool>();
                //start background task of blocks deletion
                std::unique_lock guard(_background_task_acc);
                _background_task = threads.async(
                    [this, del_scope = std::move(ready_to_delete)](std::future<void>&& previous) {
                        try
                        {
                            for (const auto& ready : del_scope)
                            {
                                free_single_block(ready);
                            }

                            if (previous.valid()) //chain previous results
                                previous.get();
                        }
                        catch (const std::exception& ex)
                        {
                            trap_error(ex);
                        }
                    }, std::move(_background_task));
            }
        }

        void trap_error(const std::exception& ex) noexcept
        {
            auto& logging = resolve<Logging>();
            logging.log(Logging::error, "Unhandled exception");
            logging.log(Logging::error, ex.what());

            std::unique_lock guard(_exception_trap_acc);
            if( _exception_trap.has_value() ) //something already set
            {
                logging.log(Logging::fatal, "Previous error trap not handled");
                logging.log(Logging::fatal, _exception_trap.value().what());
            }
            _exception_trap = ex;
        }

        void check_trapped_error()
        {
            std::unique_lock trap_guard(_exception_trap_acc);
            if( _exception_trap.has_value() )
            {
                auto temp = std::move(*_exception_trap);
                _exception_trap.reset();
                throw temp;
            }
        }

    };
}//ns::OP::trie::servants

#endif //_OP_TRIE_SERVANTS_TRIEALLOCATORIMPL__H_
