#ifndef _OP_TRIE_TRIE__H_
#define _OP_TRIE_TRIE__H_

#if defined(_MSC_VER)
#define _SCL_SECURE_NO_WARNINGS 1
#endif //_MSC_VER

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <stack>
#include <op/trie/Containers.h>
#include <op/vtm/FixedSizeMemoryManager.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/HeapManager.h>
#include <op/trie/TrieNode.h>
#include <op/trie/TrieIterator.h>
#include <op/trie/TrieResidence.h>
#include <op/trie/TrieRangeAdapter.h>
#include <op/trie/SectionAdapter.h>
#include <op/ranges/IteratorsRange.h>
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
        struct Trie : public std::enable_shared_from_this< Trie<TSegmentManager, Payload, initial_node_count> >
        {
        public:
            using payload_t = Payload ;
            using trie_t = Trie<TSegmentManager, payload_t, initial_node_count> ;
            using this_t = trie_t ;
            using iterator = TrieIterator<this_t>;
            using value_type = payload_t;
            using node_t = TrieNode<payload_t> ;
            using poistion_t = TriePosition ;
            using key_t = atom_string_t;
            using value_t = payload_t;
            using ordered_range_t = OrderedRange<atom_string_t, value_t>;
            using ordered_range_ptr = std::shared_ptr<ordered_range_t const>;

            virtual ~Trie()
            {
            }

            static std::shared_ptr<Trie> create_new(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                //create new file
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                //make root for trie
                OP::vtm::TransactionGuard op_g(segment_manager->begin_transaction()); //invoke begin/end write-op
                r->_topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().set_root_addr(r->new_node());

                op_g.commit();
                return r;
            }
            static std::shared_ptr<Trie> open(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                return r;
            }
            TSegmentManager& segment_manager()
            {
                return static_cast<TSegmentManager&>( OP::trie::resolve_segment_manager( *_topology_ptr ));
            }
            /**Total number of items*/
            std::uint64_t size() const
            {
                return _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().count();
            }
            node_version_t version() const
            {
                return _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().current_version();
            }
            /**Number of allocated nodes*/
            std::uint64_t nodes_count()
            {
                return _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().nodes_allocated();
            }

            iterator begin() const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                auto next_addr = _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().get_root_addr();
                iterator i(this);
                auto lres = load_iterator(next_addr, i,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); },
                    &iterator::emplace);
                for (auto next_addr = std::get<FarAddress>(lres);
                    std::get<bool>(lres) &&
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
                return iterator(this, typename iterator::end_marker_t{});
            }
            bool in_range(const iterator& check) const 
            {
                return check != end();
            }

            void next(iterator& i) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                if (std::get<bool>(sync_iterator(i)))
                {
                    _next(true, i);
                }
            }
            /**
            * Iterate next item that is not prefixed by current (`i`). In other words selct right of this or parent. In compare with
            * regular `next` doesn't enter to child way of current iterator (`i`).
            * For example:
            * Given prefixes:
            * \code
            * "abc"
            * "abc.1"
            * "abc.2"
            * "abc.222", //not a sibling since 'abc.2' has a it as a child
            * "abc.333"
            * "abcdef"
            * \endcode
            * Have following in the result:
            *  
            *  Source `i` | next(i) | next_sibling(i)
            *  -----------|---------|----------------
            *  abc        | abc.1   |  abc.1
            *  abc.1      | abc.2   |  abc.2
            *  abc.2      | abc.222 |  abc.333   (!)
            *  abc.333    | abcdef  |  abcdef
            */
            void next_sibling(iterator& i) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                
                if (!std::get<bool>(sync_iterator(i)))
                    return;    
                while(!i.is_end())
                { 
                    auto lres = load_iterator(i.back().address(), i,
                            [&i](ReadonlyAccess<node_t>&ro_node) { return ro_node->next((atom_t)i.back().key()); },
                            &iterator::update_back);
                    if (std::get<bool>(lres))
                    { //navigation right succeeded
                        if ((i.back().terminality() & Terminality::term_has_data) == Terminality::term_has_data)
                        {//i already points to correct entry
                            return;
                        }
                        enter_deep_until_terminal(std::get<FarAddress>(lres), i);
                        return;
                    }
                    i.pop();
                }
            }

            /** Gets next entry greater or equal than specific key. Used in `join` operations as more optimized 
            * in compare with regular `next`.
            */
            template <class AtomString>
            void next_lower_bound_of(iterator& i, const AtomString& key) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                
                if (!std::get<bool>(sync_iterator(i)))
                    return;
                //find `i` and `key` common prefix
                size_t com_prefix = 0; 
                
                for(size_t smallest = std::min(i.key().size(), key.size());
                     com_prefix < smallest && i.key()[com_prefix] == key[com_prefix]; ++com_prefix)
                {
                    /*do nothing*/    
                }
                if (!com_prefix) //no common prefix at all, just position at right bound
                {
                    i = lower_bound(key);
                    return;
                }
                i.pop_until_fit(static_cast<dim_t>(com_prefix)); //cut `i` on common prefix
                auto kbeg = key.begin()+com_prefix;
                lower_bound_impl(kbeg, key.end(), i);
            }

            using range_adapter_t = TrieRangeAdapter<this_t>;
            /**
            *   @return range that embrace all records by pair [ begin(), end() )
            */
            ordered_range_ptr range() const
            {
                return ordered_range_ptr(new range_adapter_t(this->shared_from_this()));
            }

            /** return first entry that contains prefix specified by string [begin, aend) 
            *   @param begin - first symbol of string to lookup
            *   @param aend - end of string to lookup
            *   \tparam IterateAtom iterator of string
            */
            template <class IterateAtom>
            iterator prefixed_begin(IterateAtom begin, IterateAtom aend) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator i(this);
                auto nav = common_prefix(begin, aend, i);
                if (begin != aend) //no such prefix since begin wasn't exhausted
                    return end();
                auto i_beg = i;//, i_end = i;
                //find next position that doesn't matches to prefix
                //nothing to do for: if (nav.compare_result == stem::StemCompareResult::equals //prefix fully matches to existing terminal
                if(nav.compare_result == stem::StemCompareResult::string_end) //key partially matches to some prefix
                { //correct string at the back of iterator
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
                return i_beg;
            }

            using subrange_container_t = PrefixSubrangeAdapter<this_t>;
            /**
            *   Construct a range that address all string started from string specified by [begin, aend)
            *   @param begin - first symbol of string to lookup
            *   @param aend - end of string to lookup
            *   \tparam IterateAtom iterator of string 
            */
            template <class IterateAtom>
            ordered_range_ptr prefixed_range(IterateAtom begin, IterateAtom aend) const
            {
                atom_string_t prefix(begin, aend);

                return std::static_pointer_cast<ordered_range_t const>(
                    make_mixed_range(this->shared_from_this(),
                    typename Ingredient<this_t>::PrefixedBegin( prefix ), 
                    typename Ingredient<this_t>::PrefixedLowerBound(prefix),
                    typename Ingredient<this_t>::PrefixedInRange (StartWithPredicate(prefix)) )
                );
            }
            /**
            *   Just shorthand notation for: 
            *   \code
            *   subrange(std::begin(container), std::end(container))
            *   \endcode
            * @param string any string of bytes that supports std::begin/ std::end functions
            */
            template <class AtomContainer>
            ordered_range_ptr prefixed_range(const AtomContainer& prefix) const
            {
                return std::static_pointer_cast<ordered_range_t const>(
                    make_mixed_range(this->shared_from_this(),
                        typename Ingredient<this_t>::PrefixedBegin(prefix), 
                        typename Ingredient<this_t>::PrefixedLowerBound(prefix),
                        typename Ingredient<this_t>::PrefixedInRange(StartWithPredicate(prefix)) 
                    )
                );
            }
            /**
            *   @return range that is flatten-range of all prefixes contained in param `container`.
            */
            template <class Range>
            ordered_range_ptr flatten_subrange(std::shared_ptr<Range const>& container) const
            {
                return make_flatten_range(container, [this](const auto& i) {
                    return prefixed_range(i.key());
                });
            }


            /**
            @return an iterator pointing to the first element that is not less than (i.e. greater or equal to) key 
                specified as [begin, aend).
            @param begin - specifies begin iterator of searching key
            @param aend - specifies end iterator of searching key
            */
            template <class Atom>
            iterator lower_bound(Atom& begin, Atom aend) const
            {
                auto tmp_end = end();
                return lower_bound(tmp_end, begin, aend);
            }
            /**
            *   Just shorthand notation for:
            *   \code
            *   lower_bound(std::begin(container), std::end(container))
            *   \endcode
            * @param container any enumerable bytes that supports `std::begin` / `std::end` functions
            */
            template <class AtomString>
            iterator lower_bound(const AtomString& container) const
            {
                auto beg = std::begin(container);
                return lower_bound(beg, std::end(container));
            }
            /**
            *   Returns an iterator pointing to the first element that is not less than key below string specified by param `of_prefix`.
            *   @param[in,out] of_prefix start point to lookup string. Note:
            *       \li if `of_prefix` iterator has not the same version as entire trie then 
            *           iterator is synced and at exit it may be different.
            *       \li when `of_prefix` is equal end() then function behaves like \code
            *           template <class Atom>
            *           iterator lower_bound(Atom& begin, Atom aend) const;
            *   @param[in,out] begin specify begin of finding string. In case when no matching string found 
            *           using this value allows you detect last successfully matched symbol
            *   @param[in] aend specify end of finding string. 
            */
            template <class Atom>
            iterator lower_bound(iterator& of_prefix, Atom& begin, Atom aend) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                auto sync = sync_iterator(of_prefix);
                if (std::get<bool>(sync)) 
                {
                    iterator result(of_prefix);
                    lower_bound_impl(begin, aend, result);
                    return result;
                }
                //impossible to sync (may be result of erase), let's try lower-bound of merged prefix
                auto &look_for = std::get<atom_string_t>(sync);
                auto break_at = look_for.size();
                look_for.append(begin, aend);
                iterator i2(this);
                auto b2 = look_for.begin();
                auto result = lower_bound_impl(b2, look_for.end(), i2);
                if( !i2.is_end() )
                {
                    std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(i2.key().size() - break_at);
                    begin += offset;
                }
                return i2;
            }
            /**
            *   Just shorthand notation for:
            *   \code
            *   lower_bound(of_prefix, std::begin(container), std::end(container))
            *   \endcode
            * @param container any enumerable bytes that supports `std::begin` / `std::end` functions
            */
            template <class AtomString>
            iterator lower_bound(iterator& of_prefix, const AtomString& container) const
            {
                auto b = std::begin(container);
                return lower_bound(of_prefix, b, std::end(container));
            }
            /**
                Find exact matching of string specified by [begin, aend) parameters
                @param begin of string to search
                @param aend - the end of string to search
                @return iterator that points on found string. In case if no such key result equals to `end()`
            */
            template <class Atom>
            iterator find(Atom& begin, Atom aend) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator it(this);
                if (lower_bound_impl(begin, aend, it))
                {
                    return begin == aend ? it : end();// StemCompareResult::unequals or StemCompareResult::stem_end or stem::StemCompareResult::string_end
                }
                return end();
            }
            /**
            *   Just shorthand notation for:
            *   \code
            *   find(std::begin(container), std::end(container))
            *   \endcode
            * @param container any enumerable bytes that supports `std::begin` / `std::end` functions
            */
            template <class AtomString>
            iterator find(const AtomString& container) const
            {
                auto b = std::begin(container);
                return find(b, std::end(container));
            }
            template <class AtomString>
            iterator find(iterator& of_prefix, const AtomString& container) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator iter = end ();
                if (std::get<bool>(sync_iterator(of_prefix)))
                {
                    iter = of_prefix;
                    auto begin = std::begin(container), aend = std::end(container);
                    if (lower_bound_impl(begin, aend, iter) && begin == aend)
                    {
                        return iter;
                    }
                    iter.clear();
                }
                return iter;
            }
            template <class Atom>
            iterator find(const iterator& of_prefix, Atom& begin, Atom aend) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator iter = of_prefix;
                if (std::get<bool>(sync_iterator(iter)))
                {
                    if (lower_bound_impl(begin, aend, iter) && begin == aend)
                    {
                        return iter;
                    }
                    iter.clear();
                }
                return iter;
            }
            template <class AtomString>
            iterator find(const iterator& of_prefix, const AtomString& container) const
            {
                return find(of_prefix, std::begin(container), std::end(container));
            }
            /**
            *   Quick check if some string exists in this trie.
            *   @param containser - string to check. Type must support `std::begin` / `std::end` functions
            *   @return true if exists exact string matching. Note, if trie contains only `abc`, and you check `ab` 
            *   method returns fals since it is not exact matching
            */
            template <class AtomString>
            bool check_exists(const AtomString& container) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                auto b = std::begin(container);
                iterator iter = end();
                
                auto nav_res = common_prefix(b, std::end(container), iter);

                return (nav_res.compare_result == stem::StemCompareResult::equals);
            }

            /**
            *   Get first child element resided below position specified by @param `of_this`. Since all values in Trie are lexicographically 
            *   ordred the return iterator indicate smallest immediate children
            *   For example having entries in trie: \code
            *       abc, abc.1, abc.123, abc.2, abc.3
            *   \endcode
            *   you may use `first_child(find("abc"))` to locate entry "abc.1" (compare with lower_bound that have to return "abc.123")
            *   \see last_child
            *   @return iterator to some child of_this or `end()` if no entry. 
            */
            iterator first_child(iterator& of_this) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                return position_child(of_this,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); });
            }
            /**
            *   Get last child element resided below position specified by `of_this`. Since all values in Trie are lexicographically 
            *   ordred the return iterator indicate largest immediate children
            *   For example having entries in trie: \code
            *       abc, abc.1, abc.2, abc.3, abc.333
            *   \endcode
            *   you may use `first_child(find("abc"))` to locate entry "abc.3" (compare with upper_bound that have to return "abc.333")
            *   \see last_child
            *   @param iterator that points to some prefix. After exit may be updated to reflect current state of trie
            *   @return iterator to some child of_this or `end()` if no entry.
            */
            iterator last_child(iterator& of_this) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                return position_child(of_this,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->last(); });
            }
            /**Return range that allows iterate all immediate childrens of specified prefix*/
            ordered_range_ptr children_range(const iterator& of_this) const
            {
                return std::static_pointer_cast<ordered_range_t const>(
                    make_mixed_range(
                    this->shared_from_this(), 
                    typename Ingredient<this_t>::ChildBegin{of_this}, 
                    typename Ingredient<this_t>::ChildInRange{StartWithPredicate(of_this.key())},
                    typename Ingredient<this_t>::SiblingNext{}

                ));
            }

            /**Return range that allows iterate all immediate childrens of specified prefix*/
            ordered_range_ptr sibling_range(const atom_string_t& key) const
            {
                return ordered_range_ptr( make_mixed_range(
                    this->shared_from_this(), 
                    typename Ingredient<this_t>::Find(key), 
                    typename Ingredient<this_t>::SiblingNext{}) ) ;
                
            }
            /**Return range that allows iterate all immediate childrens of specified prefix*/
           /* ordered_range_ptr sibling_range(iterator pos) const
            {
                auto zhis = this->shared_from_this();
                return ordered_range_ptr( 
                    new sibling_range_t(zhis, [pos{std::move(pos)}]() { return pos; })
                );
            }*/
            
            /** utilize feature of Trie where all entries below the single prefix are lexicographicaly ordered.
            * This range provide access to ordered sequence of suffixes. In simplified view you can think 
            * about it as a cutting right part of each string from trie
            * @param begin - specifies begin iterator of prefix that will be cut-off
            *  @param aend - specifies end iterator of prefix that will be cut-off
            */
            template <class Atom>
            ordered_range_ptr section_range(Atom begin, Atom aend) const
            {
                atom_string_t prefix{ begin, aend };
                return section_range(std::move(prefix));
            }            
            template <class AtomString>
            ordered_range_ptr section_range(AtomString prefix) const
            {
                auto source = prefixed_range(std::begin(prefix), std::end(prefix));
                using result_t = TrieSectionAdapter<typename decltype(source)::element_type>;
                return ordered_range_ptr( new result_t(source, std::move(prefix)) );
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
                    return node->get_presence(*_topology_ptr, (atom_t)position.key());
                op_g.rollback();
                throw std::invalid_argument("position has no value associated");
            }
            /**
            *   Insert string specified by pair [begin, end) and associate value with it.
            * @param begin start iterator of string to insert. 
            * @param end end iterator of string to insert.
            * @param value value to associate with inserted string
            * @return pair of iterator and success indicator. When insert succeeded iterator is a position of 
            *       just inserted item, otherwise it points to already existing key
            */
            template <class AtomIterator>
            std::pair<iterator, bool> insert(AtomIterator& begin, AtomIterator aend, Payload value)
            {
                if (begin == aend)
                    return std::make_pair(iterator(this), false); //empty string cannot be inserted

                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto value_assigner = [&]() {
                    return value;
                };
                auto on_update = [&op_g](iterator& ) {
                    op_g.rollback(); //do nothing on update TODO: check case of nested transaction if smthng is destroyed
                };
                return upsert_impl(end(), begin, aend, value_assigner, on_update);
            }
            template <class AtomContainer>
            std::pair<iterator, bool> insert(const AtomContainer& container, Payload value)
            {
                auto b = std::begin(container);
                return insert(b, std::end(container), value);
            }
            template <class AtomIterator>
            std::pair<iterator, bool> prefixed_insert(iterator& of_prefix, AtomIterator begin, AtomIterator aend, Payload value)
            {
                if (begin == aend)
                    return std::make_pair(end(), false); //empty string is not operatable

                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true/*commit automatically*/);
                auto sync_res = sync_iterator(of_prefix);
                if (!std::get<bool>(sync_res))
                { //no entry for previous iterator
                    return insert(std::get<atom_string_t>(sync_res).append(begin, aend), std::move(value));
                }
                auto value_assigner = [&]() {
                    return std::move(value);
                };
                auto on_update = [&](iterator& pos) {
                    //do nothing on update
                };
                return upsert_impl(of_prefix, begin, aend, value_assigner, on_update);
            }
            template <class AtomContainer>
            std::pair<iterator, bool> prefixed_insert(iterator& of_prefix, const AtomContainer& container, Payload payload)
            {
                return prefixed_insert(of_prefix, std::begin(container), std::end(container), payload);
            }
            /**
            *   @return number of items updated (1 or 0)
            */
            size_t update(iterator& pos, Payload && value)
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto sync_res = sync_iterator(pos);
                if (!std::get<bool>(sync_iterator(pos)) || pos.is_end())
                { //no entry for previous iterator
                    return 0;
                }
                assert(is_set(pos.back().terminality(), Terminality::term_has_data));
                //get the node in readonly way, but access values for write
                auto ro_node = view<node_t>(*_topology_ptr, pos.back().address());
                auto ridx = ro_node->reindex(*_topology_ptr, (atom_t)pos.back().key());
                auto values = _value_mngr.accessor(ro_node->payload, ro_node->capacity);
                auto &v = values[ridx];
                assert(v.has_data());
                v.set_data(std::move(value));
                _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>()
                    .increase_version() // version of trie
                    ;
                return 1;
            }
            /**Update or insert value specified by key that formed as `[begin, end)`. 
            * @param value - payload to be assigned anyway 
            * @return pair of iterator and boolean indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key. Boolean indicator is false when
            *       item already exists and true when it was inserted
            */
            template <class AtomIterator>
            std::pair<iterator, bool> upsert(AtomIterator begin, AtomIterator aend, Payload && value)
            {
                auto temp_end = end();
                return prefixed_upsert(temp_end, begin, aend, std::move(value));
            }
            /**Update or insert value specified by key. In other words
            *   place a string bellow pointer specified by iterator.
            * @param key - a container that supports std::begin/std::end semantic
            * @param value - payload to be assigned anyway 
            * @return pair of iterator and boolean indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key. Boolean indicator is false when
            *       item already exists and true when it was inserted
            */
            template <class AtomContainer>
            std::pair<iterator, bool> upsert(const AtomContainer& key, Payload&& value)
            {
                return upsert(std::begin(key), std::end(key), std::move(value));
            }
            /**Update or insert value specified by key that formed as `prefix.key + [begin, end)`. In other words
            *   place a string bellow pointer specified by iterator. 
            * @param value - payload to be assigned anyway 
            * @return pair of iterator and boolean indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key. Boolean indicator is false when
            *       item already exists and true when it was inserted
            */
            template <class AtomIterator>
            std::pair<iterator, bool> prefixed_upsert(iterator& of_prefix, AtomIterator begin, AtomIterator aend, Payload value)
            {
                if (begin == aend)
                    return std::make_pair(end(), false); //empty string is not operatable
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true/*commit automatically*/);
                auto sync_res = sync_iterator(of_prefix);
                if (!std::get<bool>(sync_res))
                { //no entry for previous iterator
                    return insert(std::get<atom_string_t>(sync_res).append(begin, aend), std::move(value));
                }
                auto value_assigner = [&]() {
                    return std::move(value);
                };
                auto on_update = [&](iterator& pos) {
                    assert(is_set(pos.back().terminality(), Terminality::term_has_data));
                    //get the nodein readonly way, but access values for write
                    auto ro_node = view<node_t>(*_topology_ptr, pos.back().address());
                    auto ridx = ro_node->reindex(*_topology_ptr, (atom_t)pos.back().key());
                    auto values = _value_mngr.accessor(ro_node->payload, ro_node->capacity);
                    auto &v = values[ridx];
                    assert(v.has_data());
                    v.set_data(std::move(value));
                };
                return upsert_impl(of_prefix, begin, aend, value_assigner, on_update);
            }
            template <class AtomContainer>
            std::pair<iterator, bool> prefixed_upsert(iterator& prefix, const AtomContainer& container, Payload value)
            {
                return prefixed_upsert(prefix, std::begin(container), std::end(container), value);
            }

            iterator erase(iterator& pos, size_t * count = nullptr)
            {
                if (count) { *count = 0; }
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                
                if (!std::get<bool>(sync_iterator(pos)) || pos.is_end())
                    return end();
                auto result{ pos };
                ++result ;
                const auto root_addr = _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().get_root_addr();
                bool erase_child_and_exit = false; //flag mean stop iteration
                for (bool first = true; pos.deep() ; pos.pop(), first = false)
                {
                    auto& back = pos.back();
                    auto wr_node = accessor<node_t>(*_topology_ptr, back.address());
                    if (erase_child_and_exit)
                    {//previous node may leave reference to child
                        wr_node->set_child(*_topology_ptr, static_cast<atom_t>(back.key()), FarAddress());
                        back._terminality &= ~Terminality::term_has_child;
                    }
                    
                    if (!wr_node->erase(*_topology_ptr, static_cast<atom_t>(back.key()), first))
                    { //don't continue if exists child node
                        back._terminality &= ~Terminality::term_has_data;
                        break;
                    }
                    //remove node if not root
                    if (back.address() != root_addr)
                    {
                        remove_node(back.address(), *wr_node);
                        erase_child_and_exit = true;
                    }
                }
                _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>()
                    .increase_count(-1) //number of terminals
                    .increase_version() // version of trie
                    ;
                if (count) { *count = 1; }
                return result;
            }
            /**Simplfied form of erase(iterator&, size_t*)*/
            iterator erase(iterator && pos)
            {
                iterator snapshot = std::move(pos);
                return erase(snapshot, nullptr);
            }
            /**Erase every entries that begins with prfix specified by iterator. If trie contains entry excatly matched to 
            * prefix then it is erased as well
            * @param prefx{in,out} - iterator to erase, at exit contains synced iterator (the same version as entire Trie)
            * @return number of erased items
            */
            size_t prefixed_erase_all(iterator& prefix)
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                if (prefix.is_end())
                {
                    return 0;
                }
                auto sync_res = sync_iterator(prefix);
                if (!std::get<bool>(sync_res))
                { //prefix totaly absent, so return nothing
                    return 0;
                }
                if (is_not_set(prefix.back().terminality(), Terminality::term_has_child))
                { //no child below, so skip any erase
                    size_t counter = 0;
                    prefix = erase(prefix, &counter);
                    return counter;
                } 
                auto parent_wr_node = accessor<node_t>(*_topology_ptr, prefix.back().address());
                auto back = classify_back(parent_wr_node, prefix);
                std::stack<FarAddress> to_process;
                to_process.push(std::get<FarAddress>(back));
                std::int64_t erased_terminals = 0;
                while (!to_process.empty())
                {
                    auto node_addr = to_process.top();
                    to_process.pop();

                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    auto node_data = _value_mngr.accessor(wr_node->payload, wr_node->capacity);
                    
                    for (auto res = wr_node->first(); res.first; res = wr_node->next(res.second))
                    {
                        auto idex = wr_node->reindex(*_topology_ptr, res.second);
                        auto& v = node_data[idex];
                        if (v.has_child())
                        {
                            to_process.push(v.get_child());
                        }
                        if (v.has_data())
                        {
                            --erased_terminals;
                        }
                        v.clear();
                    }
                    remove_node(node_addr, *wr_node);
                }
                parent_wr_node->set_child(*_topology_ptr, static_cast<atom_t>(prefix.back().key()), FarAddress());
                auto& residence = _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>()
                    .increase_count(erased_terminals) //number of terminals
                    .increase_version() // version of trie
                    ;
                prefix._version = residence.current_version();
                prefix.back()._version = parent_wr_node->version;
                prefix.back()._terminality &= ~Terminality::term_has_child;
                return std::abs(erased_terminals);
            }
            /**
            *   Remove all that starts with prefix
            */
            template <class AtomContainer>
            size_t prefixed_key_erase_all(const AtomContainer& prefix)
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                size_t result = 0;
                for (iterator it(this->lower_bound(prefix)); it != this->end(); )
                {
                    auto key_beg = std::begin(it.key());
                    auto prefix_end = std::end(prefix);

                    if (it.key().size() < prefix.size()) {
                        //check if prefix lexically_less than it.key => stop
                        auto key_end = std::end(it.key());
                        if (std::mismatch(key_beg, key_end, std::begin(prefix), prefix_end).first == key_end) //case when key contained in prefix => just skip
                        {
                            ++it;
                            continue;
                        }
                        break; //all other prefixes doesn't match and lexically bigger
                    }
                    //check found starts with 'prefix'
                    if (std::mismatch(key_beg, key_beg + prefix.size(), std::begin(prefix), prefix_end).second == prefix_end) 
                    { //key starts from prefix
                        result += prefixed_erase_all(it);
                    }
                    else
                        break;
                }
                return result;
            }
            /**
            *   Allows to apply multiple modification operations in a transaction. In fact this just decoration 
            * for \code
            *  OP::vtm::TransactionGuard op_g(segment_manager().begin_transaction(), true);
            *  try{
            *  .. multiple operations that modifies trie ...
            *  }catch(...){
            *       op_g.rollback();
            *       throw;
            *  }
            * \endcode
            */
            template <class F>
            auto apply(F& f) -> typename std::result_of<F(this_t&)>::type
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //autocommit on return
                try{
                    return f(*this);
                }catch(...){
                    op_g.rollback();
                    throw;
                }
            }
            /**
            *   Allows to apply multiple readonly operations in a transaction. In fact this just decoration 
            * for \code
            *  OP::vtm::TransactionGuard op_g(segment_manager().begin_transaction(), false);
            *  .. multiple operations that modifies trie ...
            *  op_g.commit()
            * \endcode
            */
            template <class F>
            auto apply(F const & f) const -> typename std::result_of<F(const this_t&)>::type
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //autocommit on return
                return f(*this);
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

        private:
            Trie(std::shared_ptr<TSegmentManager>& segments) noexcept
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
                auto node_pos = _topology_ptr->OP_TEMPL_METH(slot)<node_manager_t>().allocate();
                auto node = _topology_ptr->segment_manager().template wr_at<node_t>(node_pos);
                //auto node = new (node_block.pos()) node_t;

                // create hash-reindexer
                node->reindexer.address = _hash_mngr.create(capacity);
                node->capacity = (dim_t)capacity;
                //create stem-container
                auto cr_result = std::move(_stem_mngr.create(
                    (dim_t)capacity,
                    TrieDef::max_stem_length_c));
                node->stems = std::get<typename node_t::ref_stems_t>(cr_result);

                node->payload = _value_mngr.create((dim_t)capacity);

                auto &res = _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>();
                res.increase_nodes_allocated(+1);
                return node_pos;
            }
            void remove_node(FarAddress addr, node_t& node)
            {
                _hash_mngr.destroy(node.reindexer);
                _stem_mngr.destroy(node.stems);
                _value_mngr.destroy(node.payload);
                _topology_ptr->OP_TEMPL_METH(slot)<node_manager_t>().deallocate(addr);
                auto &res = _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>();
                res.increase_nodes_allocated(-1);
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
            /**Place string to node without any additional checks*/
            template <class AtomIterator, class FValueAssigner>
            void unconditional_insert(FarAddress node_addr, AtomIterator& begin, AtomIterator end, iterator& result, FValueAssigner fassign)
            {
                while (begin != end)
                {
                    auto key = *begin;
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    auto local_begin = begin;
                    wr_node->insert(*_topology_ptr, begin, end, fassign);
                    auto deep = static_cast<dim_t>(begin - local_begin);
                    result._emplace(
                        TriePosition(wr_node.address(), (atom_t)*local_begin/*key*/, deep, wr_node->version,
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
                _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>()
                    .increase_count(+1) //number of terminals
                    .increase_version() // version of trie
                    ;
            }
            /**Insert or update value associated with specified key. The value is passed as functor evaluated on demand.
            * @param start_from - iterator to start, note it MUST be already synced. At exit
            * @return pair of iterator and success indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key
            */
            template <class AtomIterator, class FValueEval, class FOnUpdate>
            std::pair<iterator, bool> upsert_impl(const iterator& start_from, AtomIterator begin, AtomIterator end, FValueEval f_value_eval, FOnUpdate f_on_update)
            {
                auto result = std::make_pair(start_from, true/*suppose insert succeeded*/);
                auto& iter = result.first;
                auto origin_begin = begin;
                auto pref_res = common_prefix(begin, end, iter);
                switch (pref_res.compare_result)
                {
                case stem::StemCompareResult::equals:
                    f_on_update(iter);
                    result.second = false;
                    return result; //dupplicate found
                case stem::StemCompareResult::no_entry:
                {
                    unconditional_insert(pref_res.child_node, begin, end, iter, f_value_eval);
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
                    unconditional_insert(node_addr, begin, end, iter, f_value_eval);
                    return result;
                }
                case stem::StemCompareResult::unequals:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    auto &back = iter.back();
                    FarAddress node_addr = back.address();
                    //entry should exists
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    if (origin_begin != begin)
                    { //need split stem 
                        node_addr =
                            diversificate(*wr_node, iter);
                    }
                    else {// nothing to diversificate
                        assert(pref_res.child_node.is_nil());
                        node_addr = new_node();
                        wr_node->set_child(*_topology_ptr, (atom_t)back.key(), node_addr);
                    }
                    unconditional_insert(node_addr, begin, end, iter, f_value_eval);
                    return result;
                }
                case stem::StemCompareResult::string_end:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    //case about non-existing item
                    _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>()
                        .increase_count(1) //number of terminals
                        .increase_version() // version of trie
                        ;

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
                                if ((back.deep() - 1) == stem_header.stem_length[rindex])
                                {
                                    //it is allowed only to have child, if 'has_data' set - then `common_prefix` works wrong
                                    assert(!(back.terminality()&Terminality::term_has_data));
                                    //don't do anything, wait until wr_node->set_value
                                }
                                else
                                {
                                    diversificate(*wr_node, iter);
                                }
                            }
                        });
                    }
                    wr_node->set_value(*_topology_ptr, static_cast<atom_t>(back.key()), std::forward<Payload>(f_value_eval()));
                    iter.back()._terminality |= Terminality::term_has_data;
                    return result;
                }
                default:
                    assert(false);
                    result.second = false;
                    return result; //fail stub
                }
            }
            /**Return not fully valid iterator that matches common part specified by [begin, end)*/
            template <class Atom>
            typename node_t::nav_result_t common_prefix(Atom& begin, Atom end, iterator& result_iter) const
            {
                typename node_t::nav_result_t retval{};
                if (end == begin)
                {
                    return retval;
                }
                FarAddress node_addr;
                if (!result_iter.is_end()) //discover from previously positioned iterator
                {
                    //there is great assumption that result_iter's deep points to full stem
                    auto cls = classify_back(result_iter);
                    if (!std::get<1>(cls)) //no way down
                    {
                        return retval;//end()
                    }
                    node_addr = std::get<FarAddress>(cls);
                }
                else
                { //start from root node
                    node_addr = _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().get_root_addr();
                }
                for (;;)
                {
                    auto node =
                        view<node_t>(*_topology_ptr, node_addr);

                    atom_t key = *begin;
                    retval = 
                        node->navigate_over(*_topology_ptr, begin, end, node_addr, result_iter);

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
            template <class FChildLocator>
            iterator position_child(iterator& of_this, FChildLocator locator) const
            {
                if (!std::get<bool>(sync_iterator(of_this)) || of_this == end() ||
                    is_not_set(of_this.back().terminality(), Terminality::term_has_child))
                {
                    return end(); //no way down
                }
                iterator result(of_this);
                position_child_unsafe(result, locator);
                return result;
            }
            template <class FChildLocator>
            void position_child_unsafe(iterator& result, FChildLocator locator) const
            {
                auto cls = classify_back(result);
                auto addr = std::get<FarAddress>(cls);
                do
                {
                    auto lres = load_iterator(addr, result,
                        locator,
                        &iterator::emplace);
                    assert(std::get<0>(lres)); //empty nodes are not allowed
                    addr = std::get<1>(lres);
                } while (is_not_set(result.back().terminality(), Terminality::term_has_data));
            }
            /**
            * @return true when result matches exactly to specified string [begin, aend)
            */
            template <class Atom>
            bool lower_bound_impl(Atom& begin, Atom aend, iterator& prefix) const
            {
                auto nav_res = common_prefix(begin, aend, prefix);

                switch (nav_res.compare_result)
                {
                case stem::StemCompareResult::equals: //exact match
                    return true;
                case stem::StemCompareResult::unequals:
                {
                    if (!prefix.is_end())
                    {
                        _next(true, prefix);
                    }
                    return false; //not an exact match
                }
                case stem::StemCompareResult::no_entry:
                {
                    auto start_from = prefix.is_end()
                        ? _topology_ptr->OP_TEMPL_METH(slot)<TrieResidence>().get_root_addr()
                        : nav_res.child_node;//prefix.back().address();
                    auto lres = load_iterator(//need reload since after common_prefix may not be complete
                        start_from, prefix,
                        [&](ReadonlyAccess<node_t>& ro_node) {
                        return ro_node->next_or_this(*begin);
                    },
                        &iterator::emplace);
                    if (!std::get<bool>(lres))
                    {
                        prefix.clear();
                        return false;
                    }
                    if (is_not_set(prefix.back().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(std::get<FarAddress>(lres), prefix);
                    }
                    return false; //not an exact match
                }
                case stem::StemCompareResult::string_end:
                {
                    auto lres = load_iterator(//need reload since after common_prefix may not be complete
                        prefix.back().address(), prefix,
                        [&](ReadonlyAccess<node_t>& ro_node) {
                        return make_nullable(prefix.back().key());
                    },
                        &iterator::update_back);
                    assert(std::get<bool>(lres));
                    if (is_not_set(prefix.back().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(std::get<FarAddress>(lres), prefix);
                    }
                    return false;//not an exact match
                }
                case stem::StemCompareResult::stem_end:
                    break; //just follow
                
                }
                //when: stem::StemCompareResult::stem_end || stem::StemCompareResult::string_end ||
                //   ( stem::StemCompareResult::unequals && !iter.is_end())
                next(prefix);
                return false; //not an exact match
            }
            /**Sync iterator to reflect current version of trie
            * @return tuple, where:
            * -# bool where true means success sync (or no sync was needed) and second string has no sense,
            *         while false means inability to sync (entry was erased);
            * -# atomic_string_t - set only when #0 is falsem origin key of iterator, because on
            *       unsuccessful sync at exit iterator is damaged
            */
            std::tuple<bool, atom_string_t> sync_iterator(iterator & it) const
            {
                const auto this_ver = this->version();
                std::tuple<bool, atom_string_t> result = { true, {} };
                if (this_ver == it.version()) //no sync is needed
                    return result;
                it._version = this_ver;
                dim_t order = 0;
                size_t prefix_length = 0;
                //take each node of iterator and check against current version of real node
                for (auto i : it._position_stack)
                {
                    auto node = view<node_t>(*_topology_ptr, i.address());
                    if (node->version != i.version())
                    {
                        std::get<1>(result) = it.key(); //need make copy since next instructions corrupt the iterator
                        it._prefix.resize(prefix_length); 
                        it._position_stack.erase(it._position_stack.begin() + order, it._position_stack.end()); //cut stack
                        auto suffix_begin = std::get<1>(result).begin() + prefix_length //cut prefix str
                            ,suffix_end = std::get<1>(result).end() 
                            ;
                        if (!lower_bound_impl(suffix_begin, suffix_end, it) )
                        {
                            std::get<0>(result) = false;
                            it = end();
                            return result;
                        }
                        return result; //still get<0> == true
                    }
                    prefix_length += i.deep();
                    ++order;
                }
                return result;
            }
            
            /**
            * @return
            * \li get<0> - true if back of iterator contains data (terminal)
            * \li get<1> - true if back of iterator has child
            * \li get<2> - address of child (if present)
            *
            */
            template <class NodeView>
            std::tuple<bool, bool, FarAddress> classify_back(NodeView& ro_node, const iterator& dest) const
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
            std::tuple<bool, bool, FarAddress> classify_back(const iterator& dest) const
            {
                auto ro_node = view<node_t>(*_topology_ptr, dest.back().address());
                return classify_back(ro_node, dest);
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

                poistion_t root_pos(node_addr, pos.second, 0, ro_node->version);

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
                    if (std::get<bool>(lres))
                    { //navigation right succeeded
                        if ((i.back().terminality() & Terminality::term_has_data) == Terminality::term_has_data)
                        {//i already points to correct entry
                            return;
                        }
                        enter_deep_until_terminal(std::get<FarAddress>(lres), i);
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
        }; 

    } //ns:trie
}//ns:OP

#endif //_OP_TRIE_TRIE__H_
