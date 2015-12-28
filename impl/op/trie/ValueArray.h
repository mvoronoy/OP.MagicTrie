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
            FarAddress create(dim_t capacity)
            {
                auto& memmngr = _topology.slot<MemoryManager>();
                OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());

            }
        private:
            SegmentTopology& _topology;
        };
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_VALUEARRAY__H_