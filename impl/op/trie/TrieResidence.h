#ifndef _OP_TRIE_TRIERESIDENCE__H_
#define _OP_TRIE_TRIERESIDENCE__H_

#include <op/vtm/SegmentManager.h>
#include <op/common/Unsigned.h>
namespace OP
{
    namespace trie
    {
        using namespace ::OP::utils;
        /**
        *   Small slot to keep arbitrary Trie information in 0 segment
        */
        struct TrieResidence : public Slot
        {
            /** Plain data structure to store metainformation of trie */
            struct TrieHeader
            {
                TrieHeader()
                    : _root{}
                    , _count(0)
                    , _nodes_allocated(0)
                    , _version(0)
                {}
                /**Where root resides*/
                FarAddress _root;
                /**Total count of terminal entries*/
                std::uint64_t _count;
                /**Number of nodes (pages) allocated*/
                std::uint64_t _nodes_allocated;
                /** Total version of trie */
                std::uint64_t _version;
            };

            template <class TSegmentManager, class Payload, std::uint32_t initial_node_count>
            friend struct Trie;
        
            /**Snapshot of Trie current state*/
            TrieHeader get_header() const
            {
                return
                    *view<TrieHeader>(*_segment_manager, _segment_address);
            }

        private:
            
            FarAddress _segment_address;
            SegmentManager* _segment_manager;
        protected:
            /**
            *   Set new root node for Trie
            * \throws TransactionIsNotStarted if method called outside of transaction scope
            * \tparam F - callback in the form `void(TrieHeader&)`
            */
            template <class F>
            void update(F callback)
            {
                auto wr = accessor<TrieHeader>(*_segment_manager, _segment_address);
                callback(*wr);
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
                /* do nothing */
            }
        };
    }//ns:trie
}//ns::OP

#endif //_OP_TRIE_TRIERESIDENCE__H_
