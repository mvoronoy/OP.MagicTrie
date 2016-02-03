#ifndef _OP_TRIE_TRIERESIDENCE__H_
#define _OP_TRIE_TRIERESIDENCE__H_

#include <op/vtm/SegmentManager.h>
namespace OP
{
    namespace trie
    {
        /**
        *   Small slot to keep arbitrary Trie information in 0 segment
        */
        struct TrieResidence : public Slot
        {
            template <class TSegmentManager, class Payload, std::uint32_t initial_node_count>
            friend struct Trie;
            /**Keep address of root node of Trie*/
            FarAddress get_root_addr()
            {
                auto ro_block = get_header_block();
                return ro_block.at<TrieHeader>(0)->_root;
            }
            /**Total count of items in Trie*/
            std::uint64_t count()
            {
                auto ro_block = get_header_block();
                return ro_block.at<TrieHeader>(0)->_count;
            }
            /**Total number of nodes (pags) allocated in Trie*/
            std::uint64_t nodes_allocated()
            {
                auto ro_block = get_header_block();
                return ro_block.at<TrieHeader>(0)->_nodes_allocated;
            }
        private:
            struct TrieHeader
            {
                TrieHeader()
                    : _root{}
                    , _count(0)
                    , _nodes_allocated(0)
                {}
                /**Where root resides*/
                FarAddress _root;
                /**Total count of terminal entries*/
                std::uint64_t _count;
                /**Number of nodes (pages) allocated*/
                std::uint64_t _nodes_allocated;
            };
            ReadonlyMemoryRange get_header_block() const
            {
                return _segment_manager->readonly_block(_segment_address, sizeof(TrieHeader));
            }
            FarAddress _segment_address;
            SegmentManager* _segment_manager;
        protected:
                        /**
            *   Set new root node for Trie
            *   @throws TransactionIsNotStarted if method called outside of transaction scope
            */
            TrieResidence& set_root_addr(FarAddress new_root)
            {
                _segment_manager->wr_at<TrieHeader>(_segment_address)->_root = new_root;
                return *this;
            }
            /**Increase/decrease total count of items in Trie. 
            *   @param delta positive/negative numeric value to modify counter
            *   @throws TransactionIsNotStarted if method called outside of transaction scope
            */
            TrieResidence& increase_count(int delta)
            {
                _segment_manager->wr_at<TrieHeader>(_segment_address)->_count += delta;
                return *this;
            }

            /**Increase/decrease total number of nodes in Trie. 
            *   @param delta positive/negative numeric value to modify counter
            *   @throws TransactionIsNotStarted if method called outside of transaction scope
            */
            TrieResidence& increase_nodes_allocated(int delta)
            {
                _segment_manager->wr_at<TrieHeader>(_segment_address)->_nodes_allocated += delta;
                return *this;
            }
            //
            //  Overrides
            //
            /**Slot resides in zero-segment only*/
            bool has_residence(segment_idx_t segment_idx, SegmentManager& manager) const override
            {
                return segment_idx == 0; //true only for 0
            }
            /**Reserve enough to keep TrieHeader*/
            segment_pos_t byte_size(FarAddress segment_address, SegmentManager& manager) const override
            {
                assert(segment_address.segment == 0);
                return memory_requirement<TrieHeader>::requirement;
            }
            void on_new_segment(FarAddress segment_address, SegmentManager& manager) override
            {
                assert(segment_address.segment == 0);
                _segment_address = segment_address;
                _segment_manager = &manager;
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                *manager.wr_at<TrieHeader>(segment_address, OP::trie::WritableBlockHint::new_c)
                    = TrieHeader(); //init with null
                op_g.commit();
            }
            void open(FarAddress segment_address, SegmentManager& manager) override
            {
                assert(segment_address.segment == 0);
                _segment_manager = &manager;
                _segment_address = segment_address;
            }
            void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
            {
                
            }
        };
    }//ns:trie
}//ns::OP

#endif //_OP_TRIE_TRIERESIDENCE__H_
