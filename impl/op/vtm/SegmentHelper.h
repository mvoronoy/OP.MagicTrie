#ifndef _OP_TRIE_SEGMENTHELPER__H_
#define _OP_TRIE_SEGMENTHELPER__H_
#include <op/common/Utils.h>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
// #include <boost/thread.hpp>
#include <cassert>
#include <op/common/Utils.h>

namespace OP
{

    namespace trie
    {
        /**
        *   Structure that resides at beggining of the each segment
        */
        struct SegmentHeader
        {
            enum
            {
                align_c = SegmentDef::align_c
            };
            SegmentHeader()
            {}
            SegmentHeader(segment_pos_t segment_size) :
                segment_size(segment_size)
            {
                //static_assert(sizeof(Segment) == 8, "Segment must be sizeof 8");
                memcpy(signature, SegmentHeader::seal(), sizeof(signature));
            }
            bool check_signature() const
            {
                return 0 == memcmp(signature, SegmentHeader::seal(), sizeof(signature));
            }

            char signature[4];
            segment_pos_t segment_size;
            static const char * seal()
            {
                static const char* signature = "mgtr";
                return signature;
            }
        };

        namespace details
        {
            using namespace boost::interprocess;

            struct SegmentHelper
            {
                friend struct SegmentManager;
                SegmentHelper(file_mapping &mapping, offset_t offset, std::size_t size) :
                    _mapped_region(mapping, read_write, offset, size),
                    _avail_bytes(0)
                {
                }
                SegmentHeader& get_segment() const
                {
                    return *at<SegmentHeader>(0);
                }
                template <class T>
                T* at(segment_pos_t offset) const
                {
                    auto *addr = reinterpret_cast<std::uint8_t*>(_mapped_region.get_address());
                    //offset += align_on(s_i_z_e_o_f(Segment), Segment::align_c);
                    return reinterpret_cast<T*>(addr + offset);
                }
                std::uint8_t* raw_space() const
                {
                    return at<std::uint8_t>(OP::utils::aligned_sizeof<SegmentHeader>(SegmentHeader::align_c));
                }
                segment_pos_t available() const
                {
                    return this->_avail_bytes;
                }

                segment_pos_t to_offset(const void *memblock)
                {
                    if (!check_pointer(memblock))
                        throw trie::Exception(trie::er_invalid_block);
                    return unchecked_to_offset(memblock);
                }
                segment_pos_t unchecked_to_offset(const void *memblock)
                {
                    return static_cast<segment_pos_t> (
                        reinterpret_cast<const std::uint8_t*>(memblock) - reinterpret_cast<const std::uint8_t*>(this->_mapped_region.get_address())
                        );
                }
                std::uint8_t* from_offset(segment_pos_t offset)
                {
                    if (offset >= this->_mapped_region.get_size())
                        throw trie::Exception(trie::er_invalid_block);
                    return reinterpret_cast<std::uint8_t*>(this->_mapped_region.get_address()) + offset;
                }
                void flush(bool assync = true)
                {
                    _mapped_region.flush(0, 0, assync);
                }
                void _check_integrity()
                {
                    //guard_t l(_free_map_lock);
                    //MemoryBlockHeader *first = at<MemoryBlockHeader>(aligned_sizeof<Segment>(Segment::align_c));
                    //size_t avail = 0, occupied = 0;
                    //size_t free_block_count = 0;
                    //MemoryBlockHeader *prev = nullptr;
                    //for (;;)
                    //{
                    //    assert(first->prev_block_offset() < 0);
                    //    if (prev)
                    //        assert(prev == first->prev());
                    //    else
                    //        assert(SegmentDef::eos_c == first->prev_block_offset());
                    //    prev = first;
                    //    first->_check_integrity();
                    //    if (first->is_free())
                    //    {
                    //        avail += first->size();
                    //        assert(_free_blocks.end() != find_by_ptr(first));
                    //        free_block_count++;
                    //    }
                    //    else
                    //    {
                    //        occupied += first->size();
                    //        assert(_free_blocks.end() == find_by_ptr(first));
                    //    }
                    //    if (first->has_next())
                    //        first = first->next();
                    //    else break;
                    //}
                    //assert(avail == this->_avail_bytes);
                    //assert(free_block_count == _free_blocks.size());
                    ////occupied???
                }
            private:
                mapped_region _mapped_region;

                segment_pos_t _avail_bytes;
                /**Because free_map_t is ordered by block size there is no explicit way to find by pointer,
                this method just provide better than O(N) improvement*/
                //free_set_t::const_iterator find_by_ptr(MemoryBlockHeader* block) const
                //{
                //    auto result = _revert_free_map_index.find(block);
                //    if (result == _revert_free_map_index.end())
                //        return _free_blocks.end();
                //    return result->second;
                //    
                //}
                //void free_block_insert(MemoryBlockHeader* block)
                //{
                //    free_set_t::const_iterator ins = _free_blocks.insert(block);
                //    auto result = _revert_free_map_index.insert(revert_index_t::value_type(block, ins));
                //    assert(result.second); //value have to be unique
                //}
                //template <class It>
                //void free_block_erase(It&to_erase)
                //{
                //    auto block = *to_erase;
                //    _revert_free_map_index.erase(block);
                //    _free_blocks.erase(to_erase);
                //}
                /** validate pointer against mapped region range*/
                bool check_pointer(const void *ptr)
                {
                    auto byte_ptr = reinterpret_cast<const std::uint8_t*>(ptr);
                    auto base = reinterpret_cast<const std::uint8_t*>(_mapped_region.get_address());
                    return byte_ptr >= base
                        && byte_ptr < (base + _mapped_region.get_size());
                }
            };
            typedef std::shared_ptr<SegmentHelper> segment_helper_p;

        }//ns::details
    } //ns::trie
}   //ns::OP
#endif //_OP_TRIE_SEGMENTHELPER__H_

