#ifndef _OP_TRIE_TYPEHELPER__H_
#define _OP_TRIE_TYPEHELPER__H_

#include <op/common/typedefs.h>
#include <op/vtm/SegmentManager.h>

namespace OP
{
    namespace trie
    {
        template <class T>
        struct PersistedReference
        {
            typedef T type;
            FarAddress address;

            explicit PersistedReference(FarAddress aadr)
                : address(aadr)
            {}
            PersistedReference()
                : address{}
            {}
            template <class TSegmentManager>
            T* ref(TSegmentManager& manager)
            {
                return manager.OP_TEMPL_METH(wr_at)<T>(address);
            }
            bool is_null() const
            {
                return address == SegmentDef::far_null_c;
            }
            template <class TSegmentManager, class ... Args>
            T* construct(TSegmentManager& manager, Args&& ... args)
            {
                return new (manager.OP_TEMPL_METH(wr_at)<T>(address)) T(std::forward<Args>(args)...);
            }
        };
        template <class T>
        struct PersistedArray
        {
            typedef T type;
            FarAddress address;
            
            explicit PersistedArray(FarAddress aadr)
                : address(aadr)
            {}
            PersistedArray()
                : address{}
            {}

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
