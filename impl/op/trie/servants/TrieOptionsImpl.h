#pragma once

#ifndef _OP_TRIE_SERVANTS_TRIEOPTIONSIMPL__H_
#define _OP_TRIE_SERVANTS_TRIEOPTIONSIMPL__H_

#include <functional>
#include <filesystem>


#include <op/common/DependencyManager.h>
#include <op/common/ThreadPool.h>

#include <op/vtm/managers/BaseSegmentManager.h>

#include <op/trie/TrieOptions.h>

#include <op/trie/servants/TrieAllocatorImpl.h>
#include <op/trie/servants/LoggingImpl.h>
#include <op/trie/servants/TrieMemoryServiceImpl.h>
#include <op/trie/servants/SeviceUID.h>

namespace OP::trie::servants
{
    namespace opc = OP::common;

    /** concept of functor that evaluates Trie's node initial size depending on level */
    template <class F>
    concept eval_node_size_functor = std::invocable<F, vtm::dim_t> 
                                    && std::same_as<std::invoke_result_t<F, size_t>, vtm::dim_t>;


    class TrieOptionsImpl 
        : public TrieOptions
        , public TrieResidenceService
        , public EvalNodeSize
    {
        static vtm::dim_t eval_size(size_t level) noexcept
        {
            if(level == 0) return 256;
            if(level == 1) return 32;
            if(level == 2) return 8;
            return 4;
        }

    public:

        TrieOptionsImpl(std::filesystem::path path, OpenMode open_mode)
            : TrieOptionsImpl(&TrieOptionsImpl::eval_size, std::move(path), open_mode)
        {
        }

        template <eval_node_size_functor FEvalNodeSize>
        constexpr TrieOptionsImpl(
            FEvalNodeSize&& eval_node_size,
            const std::filesystem::path& path,
            OpenMode open_mode
            )
            : _eval_node_size(std::forward<FEvalNodeSize>(eval_node_size))
            , _open_mode(open_mode)
        {
            std::unique_ptr<vtm::SegmentManager> segment_manager;
            std::unique_ptr<vtm::AppendOnlyMemoryPool> append_pool;
            constexpr vtm::segment_pos_t addr_table_size_c =
                OP::utils::align_on(
                    OP::utils::memory_requirement<vtm::FarAddress>::array_size(static_cast<unsigned>(ServiceUID::_last_)),
                    vtm::SegmentDef::align_c
                );


            if( open_mode == OpenMode::create )
            {
                segment_manager = vtm::BaseSegmentManager::create_new(
                    path,
                    vtm::SegmentOptions().segment_size(0x110000)
                );
                auto [start_addr, space_size] = segment_manager->user_space(0);
                segment_manager->ensure_segment(segment_manager->segment_of(start_addr));
                // allocate navigation table
                auto wr_tbl = segment_manager->writable_block(start_addr, addr_table_size_c);
                _service_addresses
                        = reinterpret_cast<vtm::FarAddress*>(wr_tbl.pos());
                
                start_addr += addr_table_size_c;
                _service_addresses[ServiceUID::space_pool_service] = start_addr;
                // allocate memory pool service
                append_pool = std::make_unique<vtm::AppendOnlyMemoryPool>(start_addr, true);
            }   
            else
            {
                segment_manager = vtm::BaseSegmentManager::open(path);
                auto [start_addr, space_size] = segment_manager->user_space(0);
                auto ro_tbl = segment_manager->readonly_block(start_addr, addr_table_size_c);
                _service_addresses = reinterpret_cast<vtm::FarAddress*>(ro_tbl.pos());
                append_pool = 
                    std::make_unique<vtm::AppendOnlyMemoryPool>(_service_addresses[ServiceUID::space_pool_service]);
            }

            auto& dep_graph = _registry.resolve_dependencies(
                std::make_unique<TrieMemoryServiceImpl>(),
                std::move(append_pool),
                std::make_unique<TrieAllocatorServiceImpl>(), //plays both roles: TrieMemoryService, TrieTransactionService
                std::move(segment_manager),
                std::ref(_logging),
                _open_mode,
                std::ref(*this), //plays both roles: TrieOptions, TrieResidenceService
                //std::ref(_thread_pool)
                std::make_unique<OP::utils::ThreadPool>()
            );
            _allocator_service = &dep_graph.get<TrieAllocatorService>();
            _memory_service = &dep_graph.get<TrieMemoryService>();
            _transaction_service = &dep_graph.get<TrieTransactionService>();

            if (open_mode == OpenMode::create)
            {
                // allocate header
                auto header_addr = _allocator_service->allocate_for<TrieHeader>(AllocHint::irregular);
                _service_addresses[ServiceUID::trie_header] = header_addr;
                //cannot use `_memory_service` there because need alloc header without transaction
                _trie_header = dep_graph.get<vtm::SegmentManager>().accessor<TrieHeader>(header_addr)
                    .make_new();
            }
            else
            {
                _trie_header = &dep_graph.get<vtm::SegmentManager>().accessor<TrieHeader>(
                    _service_addresses[ServiceUID::trie_header]);
            }
        }

        [[nodiscard]] virtual const EvalNodeSize& init_node_size() const noexcept override
        {
            return *this;
        }

        [[nodiscard]] virtual TrieResidenceService& residence_service() noexcept override
        {
            return *this;    
        }
        
        [[nodiscard]] virtual TrieAllocatorService& allocator_service() noexcept override
        {
            return *_allocator_service;
        }

        [[nodiscard]] virtual TrieMemoryService& memory_service() noexcept override
        {
            return *_memory_service;    
        }

        [[nodiscard]] virtual TrieTransactionService& transaction_service() noexcept override
        {
            return *_transaction_service;
        }

        [[nodiscard]] virtual Logging& logging() noexcept override
        {
            return _logging;
        }

        ///
        ///
        ///

        [[nodiscard]] virtual trie_version_t version() const override
        {
            assert(_trie_header);
            return std::atomic_ref<trie_version_t>(_trie_header->_version).load();
        }

        [[nodiscard]] virtual vtm::FarAddress root() const override
        {
            return std::atomic_ref<vtm::FarAddress>(_trie_header->_root).load();
        }

        [[nodiscard]] virtual vtm::FarAddress residence(unsigned uid) const override
        {
            if (uid < ServiceUID::_last_)
                return std::atomic_ref<vtm::FarAddress>(_service_addresses[uid]).load();
            throw std::invalid_argument("Invalid service ID, use indexes defined in ServiceUID");
        }
        
        [[nodiscard]] virtual std::uint64_t nodes_allocated() const 
        {
            return std::atomic_ref<trie_version_t>(_trie_header->_nodes_allocated).load();
        }

        [[nodiscard]] virtual void store_residence(vtm::FarAddress address, unsigned uid) override
        {
            if (uid < ServiceUID::_last_)
                std::atomic_ref<vtm::FarAddress>(_service_addresses[uid]).store(address);
            else
                throw std::invalid_argument("Invalid service ID, use indexes defined in ServiceUID");
        }

        [[nodiscard]] virtual std::uint64_t count() const override
        {
            return std::atomic_ref<trie_version_t>(_trie_header->_count).load();
        }

    private:
        virtual TrieResidenceService& increase_version() override
        {
            assert(_trie_header);
            ++std::atomic_ref<trie_version_t>(_trie_header->_version);
            return *this;
        }

        virtual TrieResidenceService& increase_count(std::int64_t amount = 1) override
        {
            assert(_trie_header);
            std::atomic_ref<trie_version_t>(_trie_header->_count).fetch_add(amount);
            return *this;
        }

        
        virtual void increase_nodes_allocated(std::int64_t amount = 1) override
        {
            std::atomic_ref<trie_version_t>(_trie_header->_nodes_allocated).fetch_add(amount);
        }
        
        virtual void set_root(vtm::FarAddress addr) override
        {
            vtm::FarAddress null_addr{};
            std::atomic_ref<vtm::FarAddress> root(_trie_header->_root);
            if(!root.compare_exchange_strong(null_addr, addr))
                throw std::runtime_error("Race condition when setting Trie root address");
        }

    private:

        virtual vtm::dim_t init_node_size(size_t level) const noexcept override
        {
            return _eval_node_size(level);
        }

        std::function<vtm::dim_t(size_t)> _eval_node_size;
        OP::common::ServiceRegistry _registry;
        //OP::utils::ThreadPool _thread_pool; 
        OpenMode _open_mode;
        LoggingImpl _logging{};
        TrieHeader* _trie_header = nullptr;
        vtm::FarAddress* _service_addresses;
        TrieAllocatorService* _allocator_service = nullptr;
        TrieMemoryService* _memory_service = nullptr;
        TrieTransactionService* _transaction_service = nullptr;
    };

}//ns: OP::trie::servants

#endif //_OP_TRIE_SERVANTS_TRIEOPTIONSIMPL__H_
