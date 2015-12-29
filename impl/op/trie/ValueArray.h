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
            ValueArrayData(payload_t && apayload = payload_t())
                : children(SegmentDef::far_null_c)
                , data(std::forward<payload_t>(apayload))
            /**Reference to dependent children*/
            FarAddress children;
            Payload data;
        };

        template <class SegmentTopology>
        struct ValueArrayManager
        {
            ValueArrayManager(SegmentTopology& topology)
                    : _topology(topology)
                {}
            template <class Payload>
            FarAddress create(dim_t capacity, Payload && payload = Payload())
            {
                typedef ValueArrayData<Payload> vad_t;
                auto& memmngr = _topology.slot<MemoryManager>();
                OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                memmngr.make_array<vad_t>(capacity, std::forward(payload));
                g.commit();
            }
            template <class Payload>
            void put_data(FarAddress array_addr, dim_t index, Payload && new_value)
            {
                typedef ValueArrayData<Payload> vad_t;
                OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                array_addr += index * memory_requirement<vad_t>::requirement;
                auto wr = _topology.segment_manager().wr_at<vad_t>(array_addr);
                wr.data = std::move(new_value);
                g.commit();
            }
        private:
            SegmentTopology& _topology;
        };
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_VALUEARRAY__H_