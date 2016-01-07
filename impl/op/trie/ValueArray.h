#ifndef _OP_TRIE_VALUEARRAY__H_
#define _OP_TRIE_VALUEARRAY__H_

#include <OP/trie/typedefs.h>
#include <OP/trie/SegmentManager.h>

namespace OP
{
    namespace trie
    {
        struct EmptyPayload{};
        template <class Payload>
        struct ValueArrayData
        {
            typedef Payload payload_t;
            ValueArrayData(payload_t && apayload = payload_t()) OP_NOEXCEPT
                : child(SegmentDef::far_null_c)
                , data(std::forward<payload_t>(apayload))
                , presence(no_data_c)
            {}
            ValueArrayData(ValueArrayData && other) OP_NOEXCEPT
                : child(other.child)
                , data(std::move(other.data)) //in hope that payload supports move
                , presence(other.presence)
            {
                other.presence = no_data_c; //clear previous
            }

            ValueArrayData& operator = (ValueArrayData && other) OP_NOEXCEPT
            {
                child = other.child;
                data = std::move(other.data); //in hope that payload supports move
                presence = other.presence;
                other.presence = no_data_c; //clear previous
                return *this;
            }
            void clear()
            {
                data.~Payload();
                child = FarAddress();
                presence = no_data_c;
            }
            FarAddress get_child() const
            {
                return presence & has_child_c ? child : FarAddress();
            }
            bool has_child() const
            {
                return presence & has_child_c;
            }
            bool has_data() const
            {
                return presence & has_data_c;
            }

            enum : std::uint8_t
            {
                no_data_c   = 0,
                has_child_c = 0x1,
                has_data_c  = 0x2
            };

            /**Reference to dependent children*/
            FarAddress child;
            std::uint8_t presence;
            Payload data;
        };

        template <class SegmentTopology, class Payload>
        struct ValueArrayManager
        {
            typedef Payload payload_t;
            typedef ValueArrayData<payload_t> vad_t;

            ValueArrayManager(SegmentTopology& topology)
                    : _topology(topology)
                {}
            
            FarAddress create(dim_t capacity, payload_t && payload = payload_t())
            {
                auto& memmngr = _topology.slot<MemoryManager>();
                //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                auto result = memmngr.make_array<vad_t>(capacity, std::forward<payload_t>(payload));
                //g.commit();
                return result;
            }
            
            void put_data(FarAddress array_addr, dim_t index, payload_t && new_value)
            {
                //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                array_addr += index * memory_requirement<vad_t>::requirement;
                auto wr = _topology.segment_manager().wr_at<vad_t>(array_addr);
                wr->presence |= vad_t::has_data_c;
                wr->data = std::move(new_value);
                //g.commit();
            }
            void put_child(FarAddress array_addr, dim_t index, FarAddress address)
            {
                //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                array_addr += index * memory_requirement<vad_t>::requirement;
                auto wr = _topology.segment_manager().wr_at<vad_t>(array_addr);
                wr->presence |= vad_t::has_child_c;
                wr->child = address;
                //g.commit();
            }
            void put(FarAddress array_addr, dim_t index, vad_t v)
            {
                array_addr += index * memory_requirement<vad_t>::requirement;
                auto wr = _topology.segment_manager().wr_at<vad_t>(array_addr);
                *wr = std::move(v);
            }
            /**Get value by index for RO purpose*/
            vad_t get(FarAddress array_addr, dim_t index)
            {
                array_addr += index * memory_requirement<vad_t>::requirement;
                auto ro_block = _topology.segment_manager().readonly_block(
                    array_addr,  memory_requirement<vad_t>::requirement );
                return * ro_block.at<vad_t>(0);
            }
            /**Get value by index for WR purpose*/
            vad_t& getw(FarAddress array_addr, dim_t index)
            {
                array_addr += index * memory_requirement<vad_t>::requirement;
                return *_topology.segment_manager().wr_at<vad_t>(
                    array_addr);
            }
        private:
            SegmentTopology& _topology;
        };
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_VALUEARRAY__H_