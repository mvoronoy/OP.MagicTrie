#ifndef _OP_TRIE_SKPLST__H_
#define _OP_TRIE_SKPLST__H_

#include <assert.h>
#include <array>
#include <numeric>
#include <memory>
#include <op/common/Utils.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/SegmentManager.h>


namespace OP
{
    namespace trie
    {
        using namespace OP::utils;

        struct ForwardListBase
        {
            typedef far_pos_t entry_pos_t;
            ForwardListBase() :next(SegmentDef::far_null_c){}
            entry_pos_t next;
        };
        struct MemoryBlockHeader;

        /***/
        template <class Traits, size_t bitmask_size_c = 32>
        struct Log2SkipList
        {
            typedef Traits traits_t;
            typedef typename traits_t::pos_t entry_pos_t;
            typedef Log2SkipList<Traits, bitmask_size_c> this_t;

            /**
            *   Evaluate how many bytes expected to place header of this datastructure.
            */
            OP_CONSTEXPR(OP_EMPTY_ARG) static segment_pos_t byte_size()
            {
                return OP::utils::align_on(
                    static_cast<segment_pos_t>(sizeof(ForwardListBase) * bitmask_size_c),
                    SegmentHeader::align_c);
            }
            /**Format memory starting from param `start` for skip-list header
            *@return new instance of Log2SkipList
            */
            static std::unique_ptr<this_t> create_new(SegmentManager& manager, OP::trie::far_pos_t start)
            {
                auto wr = manager.writable_block(FarAddress(start), byte_size(), WritableBlockHint::new_c);
                
                //make formatting for all bunches
                new (wr.pos())ForwardListBase[bitmask_size_c];
                
                return std::make_unique<this_t>(manager, start);
            }
            /**Open existing list from starting point 'start'
            *@return new instance of Log2SkipList
            */
            static std::unique_ptr<this_t> open(SegmentManager& manager, OP::trie::far_pos_t start)
            {
                return std::make_unique<this_t>(manager, start);
            }
            /**
            *   Construct new header for skip-list.
            * \param start position where header will be placed. After this position header will occupy
            memory block of #byte_size() length
            */
            Log2SkipList(SegmentManager& manager, OP::trie::far_pos_t start) :
                _segment_manager(manager),
                _list_pos(start)
            {
                static_assert(std::is_base_of<ForwardListBase, typename traits_t::target_t>::value, "To use skip-list you need inherit T from ForwardListBase");
            }

            far_pos_t pull_not_less(traits_t& traits, typename Traits::key_t key)
            {
                static OP_CONSTEXPR(const) segment_pos_t mbh = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                auto pull_op = [this, &traits, key](size_t index){
                    FarAddress prev_pos = entry_offset_by_idx(index);
                    auto prev_block = _segment_manager.readonly_block(prev_pos, sizeof(ForwardListBase));

                    const ForwardListBase* prev_ent = prev_block.at<ForwardListBase>(0);
                    for (far_pos_t pos = prev_ent->next; !Traits::is_eos(pos); )
                    {
                        //make 2 reads to avoid overlapped-block exception
                        auto mem_header_block = _segment_manager.readonly_block(FarAddress(FreeMemoryBlock::get_header_addr(pos)), mbh);
                        const MemoryBlockHeader * mem_header = mem_header_block.at<MemoryBlockHeader>(0);
                        auto curr_block = _segment_manager.readonly_block(FarAddress(pos), sizeof(ForwardListBase));
                        const ForwardListBase* curr = curr_block.at<ForwardListBase>(0);

                        if (!traits.less(mem_header->size(), key))
                        {
                            try
                            {
                                auto wr_prev_block = _segment_manager.upgrade_to_writable_block(prev_block);
                                auto ent = wr_prev_block.at< ForwardListBase >(0);
                                ent->next = curr->next;
                                return pos;
                            }
                            catch (const OP::vtm::ConcurentLockException&)
                            {
                                //just ignore and go further
                            }
                        }
                        pos = curr->next;
                    }
                    return SegmentDef::far_null_c;
                };
                auto index = traits.entry_index(key);
                try{
                    //try to pull block from origin slot provided
                    far_pos_t result = OP::vtm::transactional_yield_retry_n<3>(pull_op, index);
                    if (result != SegmentDef::far_null_c)
                        return result;
                }
                catch (const OP::vtm::ConcurentLockException&)
                {
                    //just continue on bigger indexes
                }
                //let's start from biggest index
                for (size_t i = bitmask_size_c-1; i > index; --i)
                {
                    try{
                        //try to pull block from origin slot provided
                        far_pos_t result = OP::vtm::transactional_yield_retry_n<3>(pull_op, i);
                        if (result != SegmentDef::far_null_c)
                            return result;
                    }
                    catch (const OP::vtm::ConcurentLockException&)
                    {
                        //just continue on smaller indexes
                    }
                }
                return SegmentDef::far_null_c;
            }
            void insert(traits_t& traits, typename traits_t::pos_t t, ForwardListBase* ref, const MemoryBlockHeader* ref_memory_block)
            {
                OP_CONSTEXPR(const static) segment_pos_t mbh = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);

                auto key = ref_memory_block->size();
                auto index = traits.entry_index(key);
                
                FarAddress prev_pos = entry_offset_by_idx(index);
                auto prev_block = _segment_manager.readonly_block(prev_pos, sizeof(ForwardListBase));
                const ForwardListBase* prev_ent = prev_block.at<ForwardListBase>(0);
                auto list_insert = [&](){
                        auto wr_prev_block = _segment_manager.upgrade_to_writable_block(prev_block);
                        auto wr_prev_ent = wr_prev_block.at< ForwardListBase >(0);
                        ref->next = wr_prev_ent->next;
                        wr_prev_ent->next = t;
                };
                for (far_pos_t pos = prev_ent->next; !Traits::is_eos(pos);)
                {
                    auto mem_header_block = _segment_manager.readonly_block(FarAddress(FreeMemoryBlock::get_header_addr(pos)), mbh);
                    const MemoryBlockHeader * mem_header = mem_header_block.at<MemoryBlockHeader>(0);

                    auto curr_block = _segment_manager.readonly_block(FarAddress(pos), sizeof(typename traits_t::target_t));
                    typename traits_t::const_ptr_t curr = curr_block.at<typename traits_t::target_t>(0);

                    if (!traits.less(mem_header->size(), key))
                    {
                        list_insert();
                        return;
                    }
                    pos = curr->next;
                    prev_block = std::move(curr_block);
                }
                //there since no entries yet
                list_insert();
            }

        private:
            SegmentManager& _segment_manager;
            OP::trie::far_pos_t _list_pos;
        private:

            FarAddress entry_offset_by_idx(size_t index) const
            {
                FarAddress r(_list_pos);
                return r+=static_cast<segment_pos_t>(index * sizeof(ForwardListBase));
            }
        };
    }//trie
}//OP
#endif //_OP_TRIE_SKPLST__H_
