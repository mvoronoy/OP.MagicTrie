#ifndef _OP_TRIE_TRIE__H_
#define _OP_TRIE_TRIE__H_

#if defined(_MSC_VER)
#define _SCL_SECURE_NO_WARNINGS 1
#endif //_MSC_VER
#include <boost/uuid/random_generator.hpp>

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <op/trie/Containers.h>
#include <op/vtm/FixedSizeMemoryManager.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryManager.h>
#include <op/trie/TrieNode.h>
#include <op/trie/TrieIterator.h>
#include <op/trie/TrieResidence.h>

namespace OP
{
    namespace trie
    {
               
        /**Constant definition for trie*/
        struct TrieDef
        {
            /**Maximal length of stem*/
            static const dim_t max_stem_length_c = 255;
        };
        
        template <class Payload>
        struct SubTrie
        {
            /**start lexicographical ascending iteration over trie content. Following is a sequence of iteration:
            *   - a
            *   - aaaaaaaaaa
            *   - abcdef
            *   - b
            *   - ...
            */
                        
            virtual std::unique_ptr<SubTrie<Payload> > subtree(const atom_t*& begin, const atom_t* end) const = 0;
            
        };
        
        template <class TSegmentManager, class Payload, std::uint32_t initial_node_count = 1024>
        struct Trie
        {
        public:
            typedef Trie<TSegmentManager, payload_t, initial_node_count> this_t;
            typedef TrieIterator<this_t> iterator;
            typedef Payload payload_t;
            typedef payload_t value_type;
            typedef TrieNode<payload_t> node_t;
            typedef TrieNavigator navigator_t;

            virtual ~Trie()
            {
            }
            
            static std::shared_ptr<Trie> create_new(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                //create new file
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                //make root for trie
                OP::vtm::TransactionGuard op_g(segment_manager->begin_transaction()); //invoke begin/end write-op
                r->_topology_ptr->slot<TrieResidence>().set_root_addr(r->new_node());
                
                op_g.commit();
                return r;
            }
            static std::shared_ptr<Trie> open(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                return r;
            }
            /**Total number of items*/
            std::uint64_t size()
            {
                return _topology_ptr->slot<TrieResidence>().count();
            }
            /**Number of allocated nodes*/
            std::uint64_t nodes_count()
            {
                return _topology_ptr->slot<TrieResidence>().nodes_allocated();
            }

            navigator_t navigator_begin()
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction()); //place all RO operations to atomic scope
                auto r_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                auto root_block = _topology_ptr->segment_manager().readonly_block(r_addr, 
                    memory_requirement<node_t>::requirement);
                auto node = root_block.at<node_t>(0);
                
                return navigator_t(_topology_ptr->slot<TrieResidence>().get_root_addr(), 
                    node->presence.first_set()/*may produce nil_c*/ 
                    );
            }
            navigator_t navigator_end() const
            {
                return navigator_t(FarAddress(SegmentDef::far_null_c), dim_t(~0u));
            }
            iterator begin()
            {
                return iterator(this, navigator_begin());
            }
            iterator end()
            {
                return iterator();
            }
            void next(iterator& i)
            {
                sync_iterator(i);

            }
            value_type value_of(navigator_t pos)
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction());
                auto node = view<node_t>(*_topology_ptr, pos.address());

                op_g.commit();

            }
            template <class Atom>
            bool insert(Atom& begin, Atom end, Payload value, iterator * result = nullptr)
            {
                if (begin == end)
                    return false; //empty string cannot be inserted
                
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); 
                auto node_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                for (;;)
                {
                    auto node =
                        view<node_t>(*_topology_ptr, node_addr);
                    
                    atom_t key = *begin;
                    auto nav_res = node->navigate_over(*_topology_ptr, begin, end);
                    switch (tuple_ref<stem::StemCompareResult>(nav_res))
                    {
                    case stem::StemCompareResult::equals:
                        op_g.rollback();
                        return false; //dupplicate found
                    case stem::StemCompareResult::stem_end: //stem is over, just follow downstair to next node
                    {
                        auto next_addr = tuple_ref<FarAddress>(nav_res);
                        if (next_addr.address == SegmentDef::far_null_c)
                        {  //no way down
                            next_addr = new_node();
                            auto wr_node_block =
                                _topology_ptr->segment_manager().upgrade_to_writable_block(node);
                            auto wr_node = wr_node_block.at<node_t>(0);
                            wr_node->set_child(*_topology_ptr, key, next_addr);
                        }
                        node_addr = next_addr;
                        break;
                    }
                    case stem::StemCompareResult::string_end:
                    {
                        //terminal is not there, otherwise it would be StemCompareResult::equals
                        auto wr_node = tuple_ref<node_t*>(diversificate(node, key, tuple_ref<dim_t>(nav_res)));
                        wr_node->set_value(*_topology_ptr, key, std::forward<Payload>(value));
                        op_g.commit();
                        return true;
                    }
                    case stem::StemCompareResult::unequals:
                    {
                        auto in_stem_pos = tuple_ref<dim_t>(nav_res);
                        if (in_stem_pos == 0)//no such entry at all
                        {
                            auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                            wr_node->insert(*_topology_ptr, begin, end, std::forward<payload_t>(value));
                            if (begin == end) //fully fit to this node
                            {
                                _topology_ptr->slot<TrieResidence>().increase_count(+1);
                                op_g.commit();
                                return true;
                            }
                            //some suffix have to be accomodated yet
                            node_addr = new_node();
                            wr_node->set_child(*_topology_ptr, key, node_addr);
                        }
                        else //entry in this node should be splitted on 2
                            node_addr = tuple_ref<FarAddress>(
                                diversificate(node, key, in_stem_pos));
                        //continue in another node
                        break;
                    }
                    default:
                        assert(false);
                    }
                } //for(;;)
            }
            
            
        private:
            typedef FixedSizeMemoryManager<node_t, initial_node_count> node_manager_t;
            typedef SegmentTopology<
                TrieResidence, 
                node_manager_t, 
                MemoryManager/*Memory manager must go last*/> topology_t;
            std::unique_ptr<topology_t> _topology_ptr;
        private:
            Trie(std::shared_ptr<TSegmentManager>& segments)
            {
                _topology_ptr = std::make_unique<topology_t>(segments);
            }
            /** Create new node with default requirements. 
            * It is assumed that exists outer transaction scope.
            */
            FarAddress new_node()
            {
                auto node_pos = _topology_ptr->slot<node_manager_t>().allocate();
                auto node = _topology_ptr->segment_manager().wr_at<node_t>(node_pos);
                //auto node = new (node_block.pos()) node_t;
                
                // create hash-reindexer
                containers::PersistedHashTable<topology_t> hash_mngr(*_topology_ptr);
                node->reindexer.address = hash_mngr.create(containers::HashTableCapacity::_8);
                //create stem-container
                stem::StemStore<topology_t> stem_mngr(*_topology_ptr);
                auto cr_result = std::move(stem_mngr.create(
                    (dim_t)containers::HashTableCapacity::_8,
                    TrieDef::max_stem_length_c));
                node->stems = tuple_ref<node_t::ref_stems_t>(cr_result);
                ValueArrayManager<topology_t, payload_t> vmanager(*_topology_ptr);
                node->payload = vmanager.create((dim_t)containers::HashTableCapacity::_8);

                auto &res = _topology_ptr->slot<TrieResidence>();
                res.increase_nodes_allocated(+1);
                auto g = boost::uuids::random_generator()();
                memcpy(node->uid.uid, g.begin(), g.size());
                return node_pos;
            }
            /**Return pair of:
            * \li current node in write-mode (matched to `node_addr`);
            * \li address of new node where the tail was placed.
            */
            std::tuple<node_t*, FarAddress> diversificate(ReadonlyAccess<node_t>& node, atom_t key, dim_t in_stem_pos)
            {
                auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                //create new node to place result
                FarAddress new_node_addr = new_node();
                wr_node->move_to(*_topology_ptr, key, in_stem_pos, new_node_addr);
                wr_node->set_child(*_topology_ptr, key, new_node_addr);
                return std::make_tuple(wr_node, new_node_addr);
            }
            void sync_iterator(iterator & it)
            {

            }
        };
    }
}
#endif //_OP_TRIE_TRIE__H_
