#ifndef _OP_TRIE_NAMEDOBJECTS__H_
#define _OP_TRIE_NAMEDOBJECTS__H_

#include <op/vtm/SegmentManager.h>
namespace OP
{
    namespace trie
    {
        struct NamedObjectSlot
        {
            /**
            *   Contract that allows to attach NamedObject to segment manager.
            *   This implementation works only for 0 segment
            */
            static segment_pos_t bind(SegmentManager& manager, segment_idx_t segment_idx, segment_pos_t offset)
            {
                manager.
            };
            /**Put object allocated in specific segment to named map.
            * @throws trie::Exception with code trie::er_no_named_map if mamed map is not specified at SegmentManager creation time. Use SegmentOptions::use_named_map
            * @throws trie::Exception with code trie::er_named_map_key_too_long if 'key' exceeds 8 chars.
            * @throws trie::Exception with code trie::er_named_map_key_exists if 'key' already exists in named map.
            */
            void put_named_object(segment_idx_t segment_idx, const std::string& key, far_pos_t ptr_offset)
            {
                //only zero segment may own by named-map
                segment_helper_p s_helper = get_segment(segment_idx);
                const Segment& seg = s_helper->get_segment();
                if (seg._named_map == SegmentDef::eos_c)
                    throw trie::Exception(trie::er_no_named_map, "Named map is not specified at SegmentManager creation time. Use SegmentOptions::use_named_map");
                if (key.length() >= named_map_t::chunk_limit_c) //too long string
                    throw trie::Exception(trie::er_named_map_key_too_long);

                named_map_t *nmap = s_helper->at<named_map_t>(seg._named_map);
                auto found = nmap->find(key.begin(), key.end());
                if (found != nmap->end())
                {   //cannot not destroy previous object since have no type-information
                    throw trie::Exception(trie::er_named_map_key_exists, key.c_str());
                }
                found = nmap->insert(key);
                nmap->value(found) = ptr_offset;
            }
            /**@return object associated with named key. If named map doesn't contain specific key then nullptr is returned*/
            template<class T>
            const T* get_named_object(const std::string& key) const
            {
                //only zero segment may own by named-map
                segment_helper_p s_helepr = get_segment(0);
                const Segment &seg = s_helepr->get_segment();
                if (seg._named_map == SegmentDef::eos_c)
                    return nullptr; //no map support

                const named_map_t *nmap = from_far<named_map_t>((far_pos_t)seg._named_map);
                auto found = nmap->find(key.begin(), key.end());
                if (found == nmap->end())
                    return nullptr;
                auto offset = nmap->value(found);
                return from_far<T>(offset);
            }

        };
    }//trie
}//OP
#endif //_OP_TRIE_NAMEDOBJECTS__H_
