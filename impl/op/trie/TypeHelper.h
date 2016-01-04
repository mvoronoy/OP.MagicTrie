#ifndef _OP_TRIE_TYPEHELPER__H_
#define _OP_TRIE_TYPEHELPER__H_

#include <OP/trie/typedefs.h>
#include <OP/trie/SegmentManager.h>

namespace OP
{
    namespace trie
    {
        template <class T>
        struct PersistedReference
        {
            typedef T type;
            FarAddress address;

            template <class TSegmentManager>
            T* ref(TSegmentManager& manager)
            {
                return manager.wr_at<T>(address);
            }
            bool is_null() const
            {
                return address == SegmentDef::far_null_c;
            }
            template <class ... Args>
            T* construct(Args&& ... args)
            {
                return new (manager.wr_at<T>(address)) T(std::forward<Args>(args)...);
            }
        };
        template <class T>
        struct PersistedArray
        {
            typedef T type;
            FarAddress address;
            
            bool is_null() const
            {
                return address == SegmentDef::far_null_c;
            }
            
            template <class TSegmentManager>
            T* ref(TSegmentManager& manager, segment_pos_t index)
            {
                return manager.wr_at<T>(address) + index;
            }
        };
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_TYPEHELPER__H_
