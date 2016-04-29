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
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/HeapManager.h>
#include <op/trie/TrieNode.h>
#include <op/trie/TrieIterator.h>
#include <op/trie/TrieResidence.h>
#include <op/trie/ranges/IteratorsRange.h>

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
        struct PrefixQuery
        {
            virtual ~PrefixQuery() = default;
            
            virtual const atom_t* end() const = 0;
            /**Advance prefix iteration by 1 symbol. return actual state of iteration*/
            virtual const atom_t* advance() = 0;
            /**Return current state of iteration (the same value as #advance() returned*/
            virtual atom_t current() const = 0;
        };

        
        template <class TSegmentManager, class Payload, std::uint32_t initial_node_count = 1024>
        struct Trie 
        {
        public:
            typedef Payload payload_t;
            typedef Trie<TSegmentManager, payload_t, initial_node_count> this_t;
            typedef TrieIterator<this_t> iterator;
            typedef SuffixRange<typedef iterator> suffix_range_t;
            typedef std::unique_ptr<suffix_range_t> suffix_range_ptr;
            typedef payload_t value_type;
            typedef TrieNode<payload_t> node_t;
            typedef TriePosition poistion_t;

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

            iterator begin() const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                auto r_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                iterator result(this);
                auto ro_node = view<node_t>(*_topology_ptr, r_addr);
                if (load_iterator(ro_node, result, _resolve_leftmost(*ro_node), &iterator::emplace))
                    assert(_begin(ro_node, result, false));
                return result;
                
            }
            iterator end() const
            {
                return iterator();
            }
            void next(iterator& i) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                sync_iterator(i);
                bool way_down = true;
                while (!i.is_end())
                {
                    auto ro_node = view<node_t>(*_topology_ptr, i.back().address());
                    if (way_down)
                    {
                        if (_begin(ro_node, i, true))
                            return;
                        way_down = false;
                    }
                    if (load_iterator(ro_node, i, _resolve_next(*ro_node, &i), &iterator::update_back))
                    {
                        assert(_begin(ro_node, i, false));//enter deep if needed
                        return;
                    }
                    //here since no way neither down nor right
                    i.pop();
                }
            }
            typedef IteratorsRange<iterator> range_container_t;

            template <class IterateAtom>
            range_container_t subrange(IterateAtom begin, IterateAtom aend) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator i (this);
                auto nav = common_prefix(begin, aend, i);
                if (begin != aend) //no such prefix
                    return range_container_t(end(), end());
                //find next position that doesn't matches to prefix
                if (nav.compare_result == stem::StemCompareResult::equals) //prefix fully matches to existing terminal
                {
                    auto n = view<node_t>(*_topology_ptr, i.back().address());
                    auto beg = i; //use copy
                    assert(_begin(n, beg, false));
                    return range_container_t(i, beg);
                }
                if (nav.compare_result == stem::StemCompareResult::string_end) //prefix partially matches to some prefix
                { //correct string at back of iterator
                    i.correct_suffix(nav.stem_rest.begin(), nav.stem_rest.end());
                }
                //
                auto i_next = i; //use copy
                next(i_next);
                return range_container_t( i, i_next );
            }
            
            template <class Atom>
            iterator lower_bound(Atom& begin, Atom end) const
            {
                auto iter = iterator(this);
                auto nav_res = common_prefix(begin, end, iter);
                
                if (nav_res.compare_result == stem::StemCompareResult::equals //exact match
                    )
                    return iter;
                if (nav_res.compare_result == stem::StemCompareResult::unequals && iter.is_end())
                {
                    return iterator();
                }
                //when: stem::StemCompareResult::stem_end || stem::StemCompareResult::string_end ||
                //   ( stem::StemCompareResult::unequals && !iter.is_end())
                next(iter);
                return iter;
            }
            template <class Atom>
            iterator find(Atom& begin, Atom end) const
            {
                auto pref_res = common_prefix(begin, end);
                auto kind = tuple_ref<stem::StemCompareResult>(pref_res);
                if (kind == stem::StemCompareResult::equals //exact match
                    
                    )
                    return tuple_ref<iterator>(pref_res);
                return iterator();// StemCompareResult::unequals or StemCompareResult::stem_end or stem::StemCompareResult::string_end
            }
            value_type value_of(poistion_t pos) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto node = view<node_t>(*_topology_ptr, pos.address());
                if ( pos.key() < (dim_t)containers::HashTableCapacity::_256 )
                    return node->get_value(*_topology_ptr, (atom_t)pos.key());
                op_g.rollback();
                throw std::invalid_argument("position has no value associated");
            }
            /**
            *   @return pair, where 
            * \li `first` - has child
            * \li `second` - has value (is terminal)
            */
            std::pair<bool, bool> get_presence(poistion_t position) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto node = view<node_t>(*_topology_ptr, position.address());
                if ( position.key() < (dim_t)containers::HashTableCapacity::_256 )
                    return node->get_presence(*_topology_ptr, (atom_t)pos.key());
                op_g.rollback();
                throw std::invalid_argument("position has no value associated");
            }
            template <class AtomIterator>
            std::pair<bool, iterator> insert(AtomIterator& begin, AtomIterator end, Payload value)
            {
                auto result = std::make_pair(false, iterator(this));
                if (begin == end)
                    return result; //empty string cannot be inserted
                auto origin_begin = begin;
                auto &iter = std::get<1>(result);
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); 
                auto pref_res = common_prefix(begin, end, iter);
                FarAddress node_addr;
                switch (pref_res.compare_result)
                {
                case stem::StemCompareResult::equals:
                    op_g.rollback();
                    return result; //dupplicate found
                case stem::StemCompareResult::unequals:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    node_addr = iter.back().address();
                    {  //entry should exists
                        auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                        node_addr = tuple_ref<FarAddress>(
                            diversificate(*wr_node, (atom_t)result.second.back().key(), result.second.back().deep()));
                    }

                }
                //no break there!
                case stem::StemCompareResult::stem_end: //stem is over, just follow downstair to next node
                {
                    node_addr = pref_res.child_node;
                    if (node_addr.address == SegmentDef::far_null_c)
                    {  //no way down
                        node_addr = new_node();
                        auto wr_node_block =
                            _topology_ptr->segment_manager().upgrade_to_writable_block(node);
                        auto wr_node = wr_node_block.at<node_t>(0);
                        wr_node->set_child(*_topology_ptr, key, node_addr);
                    }
                    break;
                }
                case stem::StemCompareResult::unequals:
                {
                    auto node_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();  //just a default value, will be overriden if common_prefix not empty
                    if (!iter.is_end())
                    {
                        node_addr = iter.back().address();
                        //if (origin_begin != begin)
                        {  //entry should exists
                            auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                            node_addr = tuple_ref<FarAddress>(
                                diversificate(*wr_node, (atom_t)result.second.back().key(), result.second.back().deep()));
                        }
                    }
                    for (;;)
                    {
                        auto key = *begin;
                        auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                        auto local_begin = begin;
                        wr_node->insert(*_topology_ptr, begin, end, [&value]() { return value; });
                        auto deep = static_cast<dim_t>(begin - local_begin) ;
                        result.second.emplace(
                            TriePosition(wr_node.address(), wr_node->uid, (atom_t)*local_begin/*key*/, deep, wr_node->version,
                            (begin == end) ? term_has_data : term_has_child),
                            local_begin + 1, begin
                        );
                        if (begin == end) //fully fit to this node
                        {
                            _topology_ptr->slot<TrieResidence>().increase_count(+1);
                            op_g.commit();
                            result.first = true;
                            return result;
                        }
                        //some suffix have to be accomodated yet
                        node_addr = new_node();
                        wr_node->set_child(*_topology_ptr, key, node_addr);
                    }
                //    for (;;)
                //    {
                //        if (origin_begin == begin)//no such entry at all
                //        {
                //            auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                //            auto local_begin = begin;
                //            wr_node->insert(*_topology_ptr, begin, end, [&value]() { return value; });
                //            auto deep = static_cast<dim_t>(begin - local_begin) - 1;
                //            result.second.emplace(
                //                TriePosition(node.address(), wr_node->uid, (atom_t)*local_begin/*key*/, deep, wr_node->version,
                //                (begin == end) ? term_has_data : term_has_child),
                //                local_begin + 1, begin
                //            );
                //            if (begin == end) //fully fit to this node
                //            {
                //                _topology_ptr->slot<TrieResidence>().increase_count(+1);
                //                op_g.commit();
                //                result.first = true;
                //                return result;
                //            }
                //            //some suffix have to be accomodated yet
                //            node_addr = new_node();
                //            wr_node->set_child(*_topology_ptr, key, node_addr);
                //        }
                //    }
                //    else //entry in this node should be splitted on 2
                //        node_addr = tuple_ref<FarAddress>(
                //            diversificate(node, key, result.second.back().deep()));
                //    //continue in another node
                //    break;
                }
                default:
                    break;
                }

                auto node_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                for (;;)
                {
                    auto node =
                        view<node_t>(*_topology_ptr, node_addr);
                    auto origin_begin = begin;

                    atom_t key = *begin;
                    auto nav_res = node->navigate_over(*_topology_ptr, begin, end, node_addr, result.second);
                    switch (nav_res.compare_result)
                    {
                    case stem::StemCompareResult::equals:
                        op_g.rollback();
                        return result; //dupplicate found
                    case stem::StemCompareResult::stem_end: //stem is over, just follow downstair to next node
                    {
                        node_addr = nav_res.child_node;
                        if (node_addr.address == SegmentDef::far_null_c)
                        {  //no way down
                            node_addr = new_node();
                            auto wr_node_block =
                                _topology_ptr->segment_manager().upgrade_to_writable_block(node);
                            auto wr_node = wr_node_block.at<node_t>(0);
                            wr_node->set_child(*_topology_ptr, key, node_addr);
                        }
                        break;
                    }
                    case stem::StemCompareResult::string_end:
                    {
                        //terminal is not there, otherwise it would be StemCompareResult::equals
                        auto wr_node = tuple_ref<node_t*>(diversificate(node, key, result.second.back().deep()));
                        wr_node->set_value(*_topology_ptr, key, std::forward<Payload>(value));
                        op_g.commit();
                        result.first = true;
                        return result;
                    }
                    case stem::StemCompareResult::unequals:
                    {
                        if (origin_begin == begin)//no such entry at all
                        {
                            auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                            auto local_begin = begin;
                            wr_node->insert(*_topology_ptr, begin, end, [&value]() { return value; });
                            auto deep = static_cast<dim_t>(begin - local_begin) - 1;
                            result.second.emplace(
                                TriePosition(node.address(), wr_node->uid, (atom_t)*local_begin/*key*/, deep, wr_node->version, 
                                (begin == end) ? term_has_data : term_has_child),
                                local_begin +1, begin
                                );
                            if (begin == end) //fully fit to this node
                            {
                                _topology_ptr->slot<TrieResidence>().increase_count(+1);
                                op_g.commit();
                                result.first = true;
                                return result;
                            }
                            //some suffix have to be accomodated yet
                            node_addr = new_node();
                            wr_node->set_child(*_topology_ptr, key, node_addr);
                        }
                        else //entry in this node should be splitted on 2
                            node_addr = tuple_ref<FarAddress>(
                                diversificate(node, key, result.second.back().deep()));
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
                HeapManagerSlot/*Memory manager must go last*/> topology_t;
            std::unique_ptr<topology_t> _topology_ptr;
            containers::PersistedHashTable<topology_t> _hash_mngr;
            stem::StemStore<topology_t> _stem_mngr;
            ValueArrayManager<topology_t, payload_t> _value_mngr;
            static nullable_atom_t _resolve_leftmost (const node_t& node, iterator* = nullptr) 
            {
                return node.first();
            };
            static nullable_atom_t _resolve_next (const node_t& node, iterator* i) 
            {
                return node.next((atom_t)i->back().key());
            };
        private:
            Trie(std::shared_ptr<TSegmentManager>& segments)
                : _topology_ptr{ std::make_unique<topology_t>(segments) }
                , _hash_mngr(*_topology_ptr)
                , _stem_mngr(*_topology_ptr)
                , _value_mngr(*_topology_ptr)
            {
                
            }
            /** Create new node with default requirements. 
            * It is assumed that exists outer transaction scope.
            */
            FarAddress new_node(containers::HashTableCapacity capacity = containers::HashTableCapacity::_8)
            {
                auto node_pos = _topology_ptr->slot<node_manager_t>().allocate();
                auto node = _topology_ptr->segment_manager().wr_at<node_t>(node_pos);
                //auto node = new (node_block.pos()) node_t;
                
                // create hash-reindexer
                node->reindexer.address = _hash_mngr.create(capacity);
                node->capacity = (dim_t)capacity;
                //create stem-container
                auto cr_result = std::move(_stem_mngr.create(
                    (dim_t)capacity,
                    TrieDef::max_stem_length_c));
                node->stems = tuple_ref<node_t::ref_stems_t>(cr_result);
                
                node->payload = _value_mngr.create((dim_t)capacity);

                auto &res = _topology_ptr->slot<TrieResidence>();
                res.increase_nodes_allocated(+1);
                auto g = boost::uuids::random_generator()();
                memcpy(node->uid.uid, g.begin(), g.size());
                return node_pos;
            }
            /**
            *   
            * @return pair of:
            * \li current node in write-mode (matched to `node_addr`);
            * \li address of new node where the tail was placed.
            * \tparam RAccess - ReadonlyAccess<node_t> or  
            */
            std::tuple<node_t*, FarAddress> diversificate(ReadonlyAccess<node_t>& node, atom_t key, dim_t in_stem_pos)
            {
                auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                return diversificate(*wr_node, key, in_stem_pos);
            }
            std::tuple<node_t*, FarAddress> diversificate(node_t& wr_node, atom_t key, dim_t in_stem_pos)
            {
                //create new node to place result
                FarAddress new_node_addr = new_node();
                wr_node.move_to(*_topology_ptr, key, in_stem_pos, new_node_addr);
                wr_node.set_child(*_topology_ptr, key, new_node_addr);
                return std::make_tuple(&wr_node, new_node_addr);
            }
            template <class AtomIterator>
            void unconditional_insert(FarAddress node_addr, AtomIterator& begin, AtomIterator end, iterator& result)
            {
                for (;;)
                {
                    auto key = *begin;
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    auto local_begin = begin;
                    wr_node->insert(*_topology_ptr, begin, end, [&value]() { return value; });
                    auto deep = static_cast<dim_t>(begin - local_begin);
                    result.second.emplace(
                        TriePosition(wr_node.address(), wr_node->uid, (atom_t)*local_begin/*key*/, deep, wr_node->version,
                        (begin == end) ? term_has_data : term_has_child),
                        local_begin + 1, begin
                    );
                    if (begin == end) //fully fit to this node
                    {
                        _topology_ptr->slot<TrieResidence>().increase_count(+1);
                        return;
                    }
                    //some suffix have to be accomodated yet
                    node_addr = new_node();
                    wr_node->set_child(*_topology_ptr, key, node_addr);
                }

            }
            /**Return not fully valid iterator that matches common part specified by [begin, end)*/
            template <class Atom>
            typename node_t::nav_result_t common_prefix(Atom& begin, Atom end, iterator& result_iter) const
            {
                auto retval = node_t::nav_result_t();
                if (end == begin)
                {
                    return retval;
                }
                auto node_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                for (;;)
                {
                    auto node =
                        view<node_t>(*_topology_ptr, node_addr);

                    atom_t key = *begin;
                    retval = node->navigate_over(*_topology_ptr, begin, end, node_addr, result_iter);
                    
                    // all cases excluding stem::StemCompareResult::stem_end mean that need return current iterator state
                    if(stem::StemCompareResult::stem_end == retval.compare_result)
                    {
                        node_addr = retval.child_node;
                        if (node_addr.address == SegmentDef::far_null_c )
                        {  //no way down, 
                            return retval; //@! it is prefix that is not satisfied lower_bound
                        }
                    }
                    else 
                        break;
                    
                } //for(;;)
                return retval;
            }
            /**If iterator has been stopped inside stem then it should be positioned at stem end*/
            void normalize_iterator(const typename node_t::nav_result_t& nav, iterator& i) const
            {

            }
            void sync_iterator(iterator & it) const
            {

            }
            /**
            * @return   
            * \li get<0> - true if back of iterator contains data (terminal)
            * \li get<1> - true if back of iterator has child
            * \li get<2> - address of child (if present)
            * 
            */
            template <class NodeView>
            std::tuple<bool, bool, FarAddress> classify_back(NodeView& ro_node, iterator& dest) const
            {
                poistion_t& pos = dest.back();
                assert(pos.key() < (dim_t)containers::HashTableCapacity::_256);
                //eval hashed index of key
                auto ridx = ro_node->reindex(*_topology_ptr, (atom_t)pos.key());
                assert(ridx < (dim_t)containers::HashTableCapacity::_256);
                
                auto values = _value_mngr.view(ro_node->payload, ro_node->capacity);
                auto &v = values[ridx];
                return std::make_tuple(v.has_data(), v.has_child(), v.get_child());
            }
            template <class NodeView, class FIteratorUpdate>
            bool load_iterator(NodeView& ro_node, iterator& dest, nullable_atom_t pos, FIteratorUpdate iterator_update) const
            {
                if (!pos.first)
                { //no first
                    return false;
                }
                //eval hashed index of key
                auto ridx = ro_node->reindex(*_topology_ptr, pos.second); 

                poistion_t root_pos(ro_node.address(), ro_node->uid, pos.second, 0, ro_node->version);

                if (!ro_node->stems.is_null())
                {//if stem exists should be placed to iterator
                    _stem_mngr.stem(ro_node->stems, ridx, 
                        [&](const atom_t* begin, const atom_t* end, const stem::StemData& stem_header){
                            root_pos._deep = static_cast<decltype(root_pos._deep)>((end - begin) + 1);//+1 means value reserved for key
                            (dest.*iterator_update)(std::move(root_pos), begin, end);
                        }
                    );
                }
                else //no stem
                {
                    (dest.*iterator_update)(std::move(root_pos), nullptr, nullptr);
                }
                return true;
            }
            /**
            * @param skip_first - true if once current iterator position should be ignored, and new value loaded (used by ::next). 
            *                     false mean that current iterator position have to be checked if enter deep is allowed (used by ::begin)
            */
            template <class NodeView>
            bool _begin(NodeView& ro_node, iterator& i, bool skip_first) const
            {
                for (auto clb = classify_back(ro_node, i);
                    skip_first || !std::get<0>(clb);)
                {
                    skip_first = false;
                    if (!std::get<1>(clb))
                    {//assume that if no data, the child must present
                        return false;
                    }
                    auto ro_node2 = view<node_t>(*_topology_ptr, std::get<2>(clb));
                    assert(load_iterator(ro_node2, i, ro_node2->first(), &iterator::emplace)); //empty nodes are not allowed
                    clb = std::move(classify_back(ro_node2, i));
                }
                return true;
            }
        };
    }
}
#endif //_OP_TRIE_TRIE__H_
