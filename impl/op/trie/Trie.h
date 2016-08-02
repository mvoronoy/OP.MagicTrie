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
#include <op/trie/ranges/PredicateRange.h>

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
                auto next_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                iterator i(this);
                auto lres = load_iterator(next_addr, i,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); },
                    &iterator::emplace);
                for (auto next_addr = tuple_ref<FarAddress>(lres);
                    tuple_ref<bool>(lres) &&
                    Terminality::term_has_data != (i.back().terminality() & Terminality::term_has_data);
                    )
                {
                    lres = load_iterator(next_addr, i,
                        [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); },
                        &iterator::emplace);
                    assert(std::get<0>(lres)); //empty nodes are not allowed
                    next_addr = std::get<1>(lres);
                }

                return i;

            }
            iterator end() const
            {
                return iterator();
            }
            bool in_range(const iterator& check) const 
            {
                return check != end();
            }

            void next(iterator& i) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                sync_iterator(i);
                _next(true, i);
            }
            typedef PredicateRange<iterator> range_container_t;
            typedef std::shared_ptr<range_container_t> range_container_ptr;

            template <class IterateAtom>
            range_container_ptr subrange(IterateAtom begin, IterateAtom aend) const
            {
                SubrangeEndPredicate<range_container_t::iterator> end_predicate(atom_string_t(begin, aend));
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator i(this);
                auto nav = common_prefix(begin, aend, i);
                if (begin != aend) //no such prefix
                    return new range_container_t(end(), AlwaysFalseRangePredicate<range_container_t::iterator>());
                auto i_beg = i;//, i_end = i;
                //find next position that doesn't matches to prefix
                //nothing to do for: if (nav.compare_result == stem::StemCompareResult::equals //prefix fully matches to existing terminal
                if(nav.compare_result == stem::StemCompareResult::string_end) //prefix partially matches to some prefix
                { //correct string at back of iterator
                    auto lres = load_iterator(i_beg.back().address(), i_beg,
                        [&i](ReadonlyAccess<node_t>& ) { 
                            return make_nullable(i.back().key());
                        },
                        &iterator::update_back);
                    assert(std::get<0>(lres));//tail must exists
                    if ((i_beg.back().terminality() & Terminality::term_has_data) != Terminality::term_has_data)
                    {
                        enter_deep_until_terminal(std::get<1>(lres), i_beg);
                    }
                }
                //
                //_next(false, i_end);
                //return range_container_t(i_beg, i_end);
                return new range_container_t(i_beg, end_predicate);
            }
            /**
            *   Just shorthand for: 
            *   \code
            *   subrange(std::begin(container), std::end(container))
            *   \endcode
            * @param string any string of bytes that supports std::begin/ std::end functions
            */
            template <class AtomContainer>
            range_container_ptr subrange(const AtomContainer& string) const
            {
                return this->subrange(std::begin(string), std::end(string));
            }

            /**
            *   @return range that is flatten-range of all prefixes contained in param `container`.
            */
            template <class Range>
            auto flatten_subrange(std::shared_ptr<Range>& container) const
            {
                return make_flatten_range(container, [this](const auto& i) {
                    return subrange(i.key());
                });
            }


            template <class Atom>
            iterator lower_bound(Atom& begin, Atom aend) const
            {
                auto iter = iterator(this);
                auto nav_res = common_prefix(begin, aend, iter);

                switch (nav_res.compare_result)
                {
                case stem::StemCompareResult::equals: //exact match
                    return iter;
                case stem::StemCompareResult::unequals:
                {
                    if (iter.is_end())
                    {
                        return iter;
                    }
                    next(iter);
                    return iter;
                }
                case stem::StemCompareResult::no_entry:
                {
                    auto start_from = iter.is_end()
                        ? _topology_ptr->slot<TrieResidence>().get_root_addr()
                        : iter.back().address();
                    auto lres = load_iterator(//need reload since after common_prefix may not be complete
                        start_from, iter,
                        [&](ReadonlyAccess<node_t>& ro_node) {
                            return ro_node->next_or_this(*begin);
                        },
                        &iterator::emplace);
                    if (!tuple_ref<bool>(lres))
                    {
                        return end();
                    }
                    if (is_not_set(iter.back().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(tuple_ref<FarAddress>(lres), iter);
                    }
                    return iter;
                }
                case stem::StemCompareResult::string_end:
                {
                    auto lres = load_iterator(//need reload since after common_prefix may not be complete
                        iter.back().address(), iter,
                        [&](ReadonlyAccess<node_t>& ro_node) {
                            return make_nullable( iter.back().key() );
                        },
                        &iterator::update_back);
                    assert(tuple_ref<bool>(lres));
                    if (is_not_set(iter.back().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(tuple_ref<FarAddress>(lres), iter);
                    }
                    return iter;
                }
                }
                //when: stem::StemCompareResult::stem_end || stem::StemCompareResult::string_end ||
                //   ( stem::StemCompareResult::unequals && !iter.is_end())
                next(iter);
                return iter;
            }
            template <class Atom>
            iterator find(Atom& begin, Atom aend) const
            {
                auto iter = lower_bound(begin, aend);
                return begin == aend ? iter : end();// StemCompareResult::unequals or StemCompareResult::stem_end or stem::StemCompareResult::string_end
            }
            template <class AtomContainer>
            iterator find(const AtomContainer& container) const
            {
                auto b = std::begin(container);
                return find(b, std::end(container));
            }

            value_type value_of(poistion_t pos) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto node = view<node_t>(*_topology_ptr, pos.address());
                if (pos.key() < (dim_t)containers::HashTableCapacity::_256)
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
                if (position.key() < (dim_t)containers::HashTableCapacity::_256)
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
                auto &iter = result.second;
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto value_assigner = [&]() {
                    _topology_ptr->slot<TrieResidence>().increase_count(+1);
                    result.first = true;
                    return value;
                };
                auto pref_res = common_prefix(begin, end, iter);
                switch (pref_res.compare_result)
                {
                case stem::StemCompareResult::equals:
                    op_g.rollback();
                    return result; //dupplicate found
                case stem::StemCompareResult::no_entry:
                {
                    unconditional_insert(pref_res.child_node, begin, end, iter, value_assigner);
                    return result;
                }
                case stem::StemCompareResult::stem_end: //stem is over, just follow downstair to next node
                {
                    assert(!iter.is_end());
                    auto &back = iter.back();
                    FarAddress node_addr = back.address();
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);

                    node_addr = new_node();
                    wr_node->set_child(*_topology_ptr, static_cast<atom_t>(back.key()), node_addr);
                    unconditional_insert(node_addr, begin, end, iter, value_assigner);
                    return result;
                }
                case stem::StemCompareResult::unequals:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    FarAddress node_addr = iter.back().address();
                    //entry should exists
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    node_addr =
                        diversificate(*wr_node, iter);
                    unconditional_insert(node_addr, begin, end, iter, value_assigner);
                    return result;
                }
                case stem::StemCompareResult::string_end:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    auto &back = iter.back();
                    FarAddress node_addr = back.address();
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    auto rindex = wr_node->reindex(*_topology_ptr, static_cast<atom_t>(back.key()));
                    if (!wr_node->stems.is_null())
                    {
                        _stem_mngr.stemw(
                            wr_node->stems,
                            rindex,
                            [&](const atom_t* sbegin, const atom_t* send, const stem::StemData& stem_header) {
                            if (sbegin != send)
                            {//empty iterator should not be diversificated, just set value
                             //terminal is not there, otherwise it would be StemCompareResult::equals
                                diversificate(*wr_node, iter);
                            }
                        });
                    }
                    wr_node->set_value(*_topology_ptr, static_cast<atom_t>(back.key()), std::forward<Payload>(value_assigner()));
                    iter.back()._terminality |= Terminality::term_has_data;
                    return result;
                }
                default:
                    assert(false);
                    return result; //fail stub
                }

#if 0                
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
#endif            
            }
            template <class AtomContainer>
            std::pair<bool, iterator> insert(const AtomContainer& container, Payload value)
            {
                auto b = std::begin(container);
                return insert(b, std::end(container), value);
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
            static nullable_atom_t _resolve_leftmost(const node_t& node, iterator* = nullptr)
            {
                return node.first();
            };
            static nullable_atom_t _resolve_next(const node_t& node, iterator* i)
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
            *  On insert to `break_position` stem may contain chain to split. This method breaks the chain
            *  and place the rest to a new children node.
            * @return address of new node
            */
            FarAddress diversificate(node_t& wr_node, iterator &break_position)
            {
                auto& back = break_position.back();
                //create new node to place result
                FarAddress new_node_addr = new_node();
                atom_t key = static_cast<atom_t>(back.key());
                dim_t in_stem_pos = back.deep();
                wr_node.move_to(*_topology_ptr, key, in_stem_pos, new_node_addr);
                wr_node.set_child(*_topology_ptr, key, new_node_addr);
                back._terminality |= Terminality::term_has_child;

                return new_node_addr;
            }
            template <class AtomIterator, class FValueAssigner>
            void unconditional_insert(FarAddress node_addr, AtomIterator& begin, AtomIterator end, iterator& result, FValueAssigner& fassign)
            {
                while (begin != end)
                {
                    auto key = *begin;
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    auto local_begin = begin;
                    wr_node->insert(*_topology_ptr, begin, end, fassign);
                    auto deep = static_cast<dim_t>(begin - local_begin);
                    result.emplace(
                        TriePosition(wr_node.address(), wr_node->uid, (atom_t)*local_begin/*key*/, deep, wr_node->version,
                        (begin == end) ? term_has_data : term_has_child),
                        local_begin + 1, begin
                    );
                    if (begin != end) //not fully fit to this node
                    {
                        //some suffix have to be accomodated yet
                        node_addr = new_node();
                        wr_node->set_child(*_topology_ptr, key, node_addr);
                    }
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
                    if (stem::StemCompareResult::stem_end == retval.compare_result)
                    {
                        node_addr = retval.child_node;
                        if (node_addr.address == SegmentDef::far_null_c)
                        {  //no way down, 
                            return retval; //@! it is prefix that is not satisfied lower_bound
                        }
                    }
                    else if (stem::StemCompareResult::no_entry == retval.compare_result)
                    {
                        retval.child_node = node_addr;
                        return retval;
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
                auto& pos = dest.back();
                assert(pos.key() < (dim_t)containers::HashTableCapacity::_256);
                //eval hashed index of key
                auto ridx = ro_node->reindex(*_topology_ptr, (atom_t)pos.key());
                assert(ridx < (dim_t)containers::HashTableCapacity::_256);

                auto values = _value_mngr.view(ro_node->payload, ro_node->capacity);
                auto &v = values[ridx];
                return std::make_tuple(v.has_data(), v.has_child(), v.get_child());
            }
            /**
            * \tparam FFindEntry - function `nullable_atom_t (ReadonlyAccess<node_t>&)` that resolve index inside node
            * \tparam FIteratorUpdate - pointer to one of iterator members - either to update 'back' position or insert new one to iterator
            * @return pair
            */
            template <class FFindEntry, class FIteratorUpdate>
            std::tuple<bool, FarAddress> load_iterator(const FarAddress& node_addr, iterator& dest, FFindEntry pos_locator, FIteratorUpdate iterator_update) const
            {
                auto ro_node = view<node_t>(*_topology_ptr, node_addr);
                nullable_atom_t pos = pos_locator(ro_node);
                if (!pos.first)
                { //no first
                    return std::make_tuple(false, FarAddress());
                }
                //eval hashed index of key
                auto ridx = ro_node->reindex(*_topology_ptr, pos.second);

                poistion_t root_pos(node_addr, ro_node->uid, pos.second, 0, ro_node->version);

                if (!ro_node->stems.is_null())
                {//if stem exists should be placed to iterator
                    _stem_mngr.stem(ro_node->stems, ridx,
                        [&](const atom_t* begin, const atom_t* end, const stem::StemData& stem_header) {
                        root_pos._deep = static_cast<decltype(root_pos._deep)>((end - begin) + 1);//+1 means value reserved for key
                        (dest.*iterator_update)(std::move(root_pos), begin, end);
                    }
                    );
                }
                else //no stem
                {
                    (dest.*iterator_update)(std::move(root_pos), nullptr, nullptr);
                }
                auto values = _value_mngr.view(ro_node->payload, ro_node->capacity);
                auto &v = values[ridx]; //take value regardless of deep
                dest.back()._terminality =
                    (v.has_data() ? Terminality::term_has_data : Terminality::term_no)
                    | (v.has_child() ? Terminality::term_has_child : Terminality::term_no);
                return std::make_tuple(true, v.get_child());
            }
            /**
            * @param skip_first - true if once current iterator position should be ignored, and new value loaded (used by ::next).
            *                     false mean that current iterator position have to be checked if enter deep is allowed (used by ::begin)
            * @return false if iterator cannot be positioned (eg. trie is empty)
            */
            bool _begin(const FarAddress& node_addr, iterator& i) const
            {
                //ensure iterator points to a valid entry (that may be result of common_prefix  unequal result)
                auto lres = load_iterator(node_addr, i,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); },
                    &iterator::update_back);
                if (!std::get<0>(lres))
                {
                    return false;
                }
                auto& back = i.back();

                assert(back.terminality() != Terminality::term_no); //it is error unpredictable state
                enter_deep_until_terminal(std::get<1>(lres), i);
                return true;
            }
            void _next(bool way_down, iterator &i) const
            {
                while (!i.is_end())
                {
                    //try enter deep
                    if (way_down && (i.back().terminality() & Terminality::term_has_child) == Terminality::term_has_child)
                    {   //get address of child
                        auto ro_node = view<node_t>(*_topology_ptr, i.back().address());
                        auto ridx = ro_node->reindex(*_topology_ptr, static_cast<atom_t>(i.back().key()));
                        auto values = _value_mngr.view(ro_node->payload, ro_node->capacity);
                        auto &v = values[ridx]; //take value regardless of deep
                        enter_deep_until_terminal(v.get_child(), i);
                        return;
                    }
                    //try navigate right from current position
                    auto lres = load_iterator(i.back().address(), i,
                        [&i](ReadonlyAccess<node_t>&ro_node) { return ro_node->next((atom_t)i.back().key()); },
                        &iterator::update_back);
                    if (tuple_ref<bool>(lres))
                    { //navigation right succeeded
                        if ((i.back().terminality() & Terminality::term_has_data) == Terminality::term_has_data)
                        {//i already points to correct entry
                            return;
                        }
                        enter_deep_until_terminal(tuple_ref<FarAddress>(lres), i);
                        return;
                    }
                    //here since no way neither down nor right
                    way_down = false;
                    i.pop();
                }
            }
            void enter_deep_until_terminal(FarAddress start_from, iterator& i) const
            {
                do
                {
                    assert(start_from.address != SegmentDef::far_null_c);
                    auto lres = load_iterator(start_from, i,
                        [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); },
                        &iterator::emplace);
                    assert(std::get<0>(lres)); //empty nodes are not allowed
                    start_from = std::get<1>(lres);
                } while (is_not_set(i.back().terminality(), Terminality::term_has_data));
            }
            /**Implement functor for subrange method to implement predicate that detects end of range iteration*/
            template <class Iterator>
            struct SubrangeEndPredicate
            {
                SubrangeEndPredicate(atom_string_t && prefix)
                    : _prefix(std::move(prefix))
                {
                }
                bool operator()(const Iterator& check) const
                {
                    auto && str = check.key();
                    if (str.length() < _prefix.length())
                        return false;
                    return std::equal(prefix.begin(), prefix.end(), str.begin());
                }
            private:
                atom_string_t _prefix;
            };
        };
    }
}
#endif //_OP_TRIE_TRIE__H_
