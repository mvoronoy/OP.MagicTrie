#pragma once

#ifndef _OP_TRIE_RESIDENCEMANAGER__H_
#define _OP_TRIE_RESIDENCEMANAGER__H_

#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/vtm/AppendOnlyMemoryPool.h>

namespace OP::trie
{
    struct ResidenceManager
    {
        using far_t = OP::vtm::FarAddress;
        using segment_pos_t = OP::vtm::segment_pos_t;
        using service_id_t = std::uint64_t;

        constexpr static inline segment_pos_t grow_size_c = 4;

        ResidenceManager(OP::vtm::SegmentManager& manager, OP::vtm::AppendOnlyMemoryPool& pool, far_t root)
            : _manager(manager)
            , _pool(pool)
            , _root(root)
        {
            assert(!root.is_nil());
            deserialize(); //place all records (if ever exists) from persistent state to cache
        }

        static [[nodiscard]] ResidenceManager create(OP::vtm::SegmentManager& manager, OP::vtm::AppendOnlyMemoryPool& pool)
        {
            return ResidenceManager(manager, pool, create_page(manager, pool));
        }

        far_t root_address() const
        {
            return _root;
        }

        /**
        * @throws std::invalid_argument if service_key already exists
        */ 
        void add_record(service_id_t service_key, far_t residence)
        {
            std::unique_lock guard(_cached_records_acc);
            auto [_, success] = _cached_records.try_emplace(service_key, residence);
            if (!success)
            {
                throw std::invalid_argument("service_key must represent unique service id");
            }
            //now I'm sure no duplicates (lock is still captured)
            far_t last_chain = _root;
            segment_pos_t last_record_index = OP::vtm::eos_c; //also valid for no-record
            for_each([&](far_t& chain_addr, segment_pos_t record_idx) -> bool {
                   // brut force all records to find last entry
                last_chain = chain_addr;
                last_record_index = record_idx;
                return true;
                });
            assert(!last_chain.is_nil()); //at least 1 page page must exists
            auto wr_chain = _manager.accessor<RecordChain>(last_chain);
            RecordChain* last = &wr_chain;
            if (++last_record_index == grow_size_c) //entry is the last in the RecordChain
            {//allocate new page
                assert(last->_next.is_nil()); //must be empty
                last->_next = create_page(_manager, _pool);
                wr_chain = _manager.accessor<RecordChain>(last->_next);
                last = &wr_chain;
                last_record_index = 0; //reset index
            }
            last->_records[last_record_index] = { ._key = service_key, ._address = residence };
        }

        std::optional<far_t> find_service(service_id_t service_key)
        {
            std::shared_lock guard(_cached_records_acc);
            auto pos = _cached_records.find(service_key);
            if (pos == _cached_records.end())
                return {};
            return pos->second;
        }

    private:
        OP::vtm::SegmentManager& _manager;
        OP::vtm::AppendOnlyMemoryPool& _pool;
        far_t _root;
        std::uint_fast8_t _grow_by;
        std::unordered_map<std::uint64_t, far_t> _cached_records;
        std::shared_mutex _cached_records_acc;

        struct ServiceRecord
        {
            std::uint64_t _key = 0;
            far_t _address{};

            constexpr bool valid() const noexcept
            {
                return !_address.is_nil();
            }
        };

        struct RecordChain
        {
            constexpr static inline std::uint16_t signature_c = ('R' << 8) | 'C';
            const std::uint16_t _signature = signature_c;

            far_t _next = {};
            ServiceRecord _records[grow_size_c]{};
        };

        static far_t create_page(OP::vtm::SegmentManager& manager, OP::vtm::AppendOnlyMemoryPool& pool)
        {
            using namespace OP::utils;

            const segment_pos_t page_size =
                memory_requirement<RecordChain>::element_size_c;

            far_t result = pool.allocate(page_size);
            auto wr_chain = manager.writable_block(result, page_size, OP::vtm::WritableBlockHint::new_c);
            RecordChain* records = ::new (wr_chain.pos()) RecordChain{ };
            return result;
        }
        
        template <class FCallback>
        void for_each(FCallback callback)
        {
            for (far_t addr = _root; !addr.is_nil();)
            {
                auto rc_block = _manager.view<RecordChain>(addr);
                const RecordChain& rc = *rc_block;
                assert(rc._signature == rc.signature_c);

                for (segment_pos_t i = 0; i < grow_size_c && rc._records[i].valid(); ++i)
                {
                    if (!OP::currying::recomb_call(callback, rc._records[i], rc, i, addr))
                        return;
                }
                addr = rc._next;
            }
        }

        void deserialize()
        {
            std::unique_lock guard(_cached_records_acc); //actually redundant since called from constructor only
            for_each([this](const ServiceRecord& service_record) -> bool {
                auto [_, succ] = _cached_records.emplace(
                    service_record._key, service_record._address);
                assert(succ); //keys must be unique
                return true; //continue iteration
                });
        }
    };


}//ns:OP::trie

#endif // _OP_TRIE_RESIDENCEMANAGER__H_
