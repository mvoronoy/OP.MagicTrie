
#pragma once

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <fstream>
#include <op/trie/Containers.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Range.h>
namespace OP
{
    namespace trie
    {
        enum  NodeType
        {
            unassigned_c = 0,
            /**Persisted node is NodeHashTable*/
            hash_c = 0x1,
            trie_c = 0x2,
            sorted_arr_c = 0x3
        };

        struct NodeAddress
        {
            NodeAddress() :
                segment(SegmentDef::eos_c),
                head(0)
            {
            }
            NodeAddress(segment_idx_t a_segment, std::uint32_t a_head) :
                segment(a_segment),
                head(a_head)
            {
            }
            segment_idx_t segment;
            /**It is order number of NodeHead inside segment*/
            std::uint32_t head;
            NodeAddress operator + (std::uint32_t off) const
            {
                return NodeAddress(segment, head + off);
            }
            NodeAddress& operator += (std::uint32_t off)
            {
                head += off;
                return *this;
            }
        };
        inline bool operator < (const NodeAddress& left, const NodeAddress& right)
        {
            if (right.segment < left.segment)
                return false;
            //there since left <= right
            return left.head < right.head;
        }
        inline bool operator == (const NodeAddress& left, const NodeAddress& right)
        {
            return (right.segment == left.segment) && (left.head == right.head);
        }

        template <class Payload>
        struct NodeHead
        {
            typedef std::fpos_t fpos_t;
            typedef NavigableByteRangeContainer<atom_t> navigation_container_t;
            typedef ByteKeyContainer<NodeAddress> address_container_t;
            typedef ByteKeyContainer<Payload> value_container_t;
            NodeHead() :
                node_type(NodeType::unassigned_c),
                navigation(SegmentDef::null_block_idx_c),
                references(SegmentDef::null_block_idx_c),
                values(SegmentDef::null_block_idx_c),
                parent(SegmentDef::far_null_c)
            {
            }
            atom_t node_type;
            far_pos_t parent;
            union
            {
                /**In-segment position of NavigableByteRangeContainer for this node*/
                segment_pos_t navigation;
                /**Only if this node-head is unassigned (node_type==unassigned_c) this points to the next free head*/
                segment_pos_t next_free;
            };
            /**In-segment position of address container for this node*/
            segment_pos_t references;
            /**In-segment position of values container for this node*/
            segment_pos_t values;
        };

        struct NodeManagerOptions
        {
            NodeManagerOptions() :
                _node_capacity(1 << 16)
            {

            }
            segment_pos_t node_count() const
            {
                return _node_capacity;
            }
            NodeManagerOptions& node_count(segment_pos_t count)
            {
                this->_node_capacity = count;
                return *this;
            }
            SegmentOptions& segment_options()
            {
                return _segment_options;
            }

        private:

            SegmentOptions _segment_options;
            segment_pos_t _node_capacity;
        };

        template <class Payload>
        struct NodeManager
        {
            enum State
            {
                /**Node of this state is not in memory*/
                void_c = 0,
                /**Node in this state can be used to find by sequence of bytes */
                findable_c = 0x1,
                /** node in this state can reference to another nodes*/
                navigable_c = 0x2,
                /** node in this state can access the associated value*/
                valueable_c = 0x4
            };
            typedef NodeManager<Payload> this_t;
            typedef NodeHead<Payload> node_head_t;
            /**Default type that implements NodeHead::navigation_container_t*/
            typedef NodeSortedArray<atom_t, 16> default_navigation_conatiner_t;

            /**Type that implements NodeHead::navigation_container_t for root node*/
            typedef NodeTrie<atom_t> root_navigation_conatiner_t;

            /**Default type that implements NodeHead::address_container_t*/
            typedef NodeHashTable<NodeAddress, 8> default_address_container_t;

            /**Default type that implements NodeHead::value_container_t*/
            typedef NodeHashTable<Payload, 8> default_value_container_t;

            static std::unique_ptr<NodeManagerOptions> create_options()
            {
                return std::unique_ptr<NodeManagerOptions>(new NodeManagerOptions);
            }
            static std::unique_ptr<NodeManager> create_new(const char * file_name, const NodeManagerOptions& options)
            {
                //clone local option
                NodeManagerOptions local_options(options);
                local_options.segment_options()
                    //declare neccessary space for segment management
                    .heuristic_size(
                    //size_heuristic::of_assorted<NodeManagementBlock, 1>,
                    size_heuristic::of_array_dyn<node_head_t>(options.node_count()),
                    size_heuristic::of_assorted<root_navigation_conatiner_t, 1>,
                    size_heuristic::of_array_dyn<default_navigation_conatiner_t>(options.node_count()),
                    size_heuristic::of_array_dyn<default_address_container_t>(options.node_count()),
                    size_heuristic::of_array_dyn<default_value_container_t>(options.node_count()),
                    size_heuristic::add_percentage(5/*Add 5% for reallocation purposes*/)
                    );
                auto segment_manager = SegmentManager::create_new(file_name, local_options.segment_options());

                //allocate array of heads
                auto node_manager = std::unique_ptr<NodeManager>(new NodeManager(segment_manager));
                node_manager->_node_capacity = options.node_count();
                node_manager->on_new_segment_allocated(0);
                return node_manager;
            }
            SegmentManager& segment_manager()
            {
                return *_segment_manager;
            }
            static std::unique_ptr<NodeManager> open_existing()
            {

            }
            std::future<node_head_t*> async_get_head(const NodeAddress& addr)
            {

            }
            far_pos_t create_new_node(far_pos_t parent = SegmentDef::far_null_c)
            {
                tran_guard_t g(this);
                _segment_manager->make_new()
            }
        protected:

            void start_transaction()
            {

            }
            void stop_transaction(bool success)
            {

            }
        private:
            typedef RangeContainer<NodeAddress> free_ranges_t;
            typedef segment_operations_guard_t guard_t;
            typedef std::shared_ptr<SegmentManager> segments_ptr_t;
            segments_ptr_t _segment_manager;
            /**number of nodes per 1 segment*/
            segment_pos_t _node_capacity;
            /**top free node*/
            NodeAddress _top_node;
            static const std::string nm_headers_c;
            static const std::string nm_headers_free_c;
            /**On new segment allocation need place some objects inside it
            */
            void on_new_segment_allocated(segment_idx_t new_segment)
            {
                guard_t l(_segment_manager.get());
                //auto node_array_off = _segment_manager->make_array<node_head_t>(new_segment, _node_capacity);
                //auto node_array = _segment_manager->from_far<node_head_t>(node_array_off);
                ////make all new free block reference in linear list
                //for (segment_pos_t i = 0; i < (_node_capacity - 1); ++i)
                //    node_array[i].next_free = i + 1;
                //_segment_manager->put_named_object(new_segment, nm_headers_c, node_array_off);
                ////_segment_manager->put_named_object(new_segment, nm_headers_free_c, &node_array[0]/*pointer to first free header*/);
                //l.commit();
            }
            NodeManager(segments_ptr_t segment_manager)
            {
                _segment_manager = segment_manager;
            }

        };
        template <class Payload>
        const std::string NodeManager<Payload>::nm_headers_c("hdr");
        template <class Payload>
        const std::string NodeManager<Payload>::nm_headers_free_c("frhdr");
    }
}//endof namespace OP
