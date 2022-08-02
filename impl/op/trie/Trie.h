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
#include <op/vtm/StringMemoryManager.h>

namespace OP
{
    namespace trie
    {

        /**Constant definition for trie*/
        struct TrieOptions
        {
            /**Maximal length of stem*/
            dim_t init_node_size(size_t level) const
            {
                return level == 0 ? 256 : _node_size;
            }
        private:
            dim_t _node_size = 8;
        };


        template <class TSegmentManager, class Payload, std::uint32_t initial_node_count = 1024>
        struct Trie : public std::enable_shared_from_this< Trie<TSegmentManager, Payload, initial_node_count> >
        {
        public:
            using payload_t = Payload;
            using trie_t = Trie<TSegmentManager, payload_t, initial_node_count>;
            using this_t = trie_t;
            using iterator = TrieIterator<this_t>;
            using value_type = payload_t;
            using node_t = TrieNode<payload_t>;
            using position_t = TriePosition;
            using key_t = atom_string_t;
            using value_t = payload_t;


            virtual ~Trie()
            {
            }

            static std::shared_ptr<Trie> create_new(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                //create new file
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                //make root for trie
                OP::vtm::TransactionGuard op_g(segment_manager->begin_transaction()); //invoke begin/end write-op
                r->_topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().set_root_addr(r->new_node(0));

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
                return static_cast<TSegmentManager&>(OP::trie::resolve_segment_manager(*_topology_ptr));
            }
            /**Total number of items*/
            std::uint64_t size() const
            {
                return _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().count();
            }
            node_version_t version() const
            {
                return _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().current_version();
            }
            /**Number of allocated nodes*/
            std::uint64_t nodes_count()
            {
                return _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().nodes_allocated();
            }

            iterator begin() const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                auto next_addr = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().get_root_addr();
                iterator i(this);
                auto lres = load_iterator(next_addr, i,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); },
                    &iterator::emplace);
                for (auto next_addr = std::get<FarAddress>(lres);
                    std::get<bool>(lres) &&
                    is_not_set(i.rat().terminality(), Terminality::term_has_data);
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
            * "abc.222", //not a sibling since 'abc.2' has a tail as a child
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
                while (!i.is_end())
                {
                    auto lres = load_iterator(
                        i.rat().address(), i,
                        [&i](ReadonlyAccess<node_t>& ro_node)
                        {
                            //don't optimize i.rat() since iterator updated inside `load_iterator`
                            return ro_node->next((atom_t)i.rat().key());
                        },
                        &iterator::update_back);
                    if (std::get<bool>(lres))
                    { //navigation right succeeded
                        if (all_set(i.rat().terminality(), Terminality::term_has_data))
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

                for (size_t smallest = std::min(i.key().size(), key.size());
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
                auto kbeg = key.begin() + com_prefix;
                lower_bound_impl(kbeg, key.end(), i);
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
                //nothing to do for: if (nav.compare_result == StemCompareResult::equals //prefix fully matches to existing terminal
                if (nav.compare_result == StemCompareResult::string_end) //key partially matches to some prefix
                { //correct string at the back of iterator
                    auto lres = load_iterator(i_beg.rat().address(), i_beg,
                        [&i](ReadonlyAccess<node_t>&) {
                            return make_nullable(i.rat().key());
                        },
                        &iterator::update_back);
                    assert(std::get<0>(lres));//tail must exists
                    if (is_not_set(i_beg.rat().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(std::get<1>(lres), i_beg);
                    }
                }
                
                return i_beg;
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
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), false); //place all RO operations to atomic scope
                auto [succeeded, lookup_key] = sync_iterator(of_prefix);
                if (succeeded)
                {
                    iterator result(of_prefix);
                    lower_bound_impl(begin, aend, result);
                    return result;
                }
                //impossible to sync (may be result of erase), let's try lower-bound of merged prefix
                auto break_at = lookup_key.size();
                lookup_key.append(begin, aend);
                iterator i2(this);
                auto b2 = lookup_key.begin();
                auto result = lower_bound_impl(b2, lookup_key.end(), i2);
                if (!i2.is_end())
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
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), false); //place all RO operations to atomic scope
                iterator it(this);
                if (lower_bound_impl(begin, aend, it))
                {
                    return begin == aend ? it : end();// StemCompareResult::unequals or StemCompareResult::stem_end or StemCompareResult::string_end
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
                iterator iter = end();
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
            //template <class AtomString>
            //iterator find(const iterator& of_prefix, const AtomString& container) const
            //{
            //    return find(of_prefix, std::begin(container), std::end(container));
            //}
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

                return (nav_res.compare_result == StemCompareResult::equals);
            }

            /**
            *   Get first child element resided below position specified by @param `of_this`. Since all keys in Trie are lexicographically
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
            *   Get last child element resided below position specified by `of_this`. Since all keys in Trie are lexicographically
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
            /**
            *   @return sequence-factory that embrace all records by pair [ begin(), end() )
            */
            auto range() const
            {
                return OP::flur::make_lazy_range(
                    TrieSequenceFactory<this_t>(this->shared_from_this()));
            }

            /**
            *   Construct a range that address all string started from string specified by [begin, aend)
            *   @param begin - first symbol of string to lookup
            *   @param aend - end of string to lookup
            *   \tparam IterateAtom iterator of string
            */
            template <class IterateAtom>
            auto prefixed_range(IterateAtom begin, IterateAtom aend) const
            {
                atom_string_t prefix(begin, aend);
                return prefixed_range(prefix);
            }
            /**
            *   Construct a range that address all string started with specified prefix
            * @param prefix any string of bytes that supports std::begin / std::end iteration
            */
            template <class AtomContainer>
            auto prefixed_range(const AtomContainer& prefix) const
            {
                return OP::flur::make_lazy_range(
                    make_mixed_sequence_factory(
                        std::const_pointer_cast<const this_t>(this->shared_from_this()),
                        typename Ingredient<this_t>::PrefixedBegin(prefix),
                        typename Ingredient<this_t>::PrefixedLowerBound(prefix),
                        typename Ingredient<this_t>::PrefixedInRange(StartWithPredicate(prefix))
                    )
                );
            }

            /**Return range that allows iterate all immediate childrens of specified prefix*/
            auto children_range(const iterator& of_this) const
            {
                return OP::flur::make_lazy_range(make_mixed_sequence_factory(
                    std::const_pointer_cast<const this_t>(this->shared_from_this()),
                    typename Ingredient<this_t>::ChildBegin{ of_this },
                    typename Ingredient<this_t>::ChildInRange{ StartWithPredicate(of_this.key()) },
                    typename Ingredient<this_t>::SiblingNext{}
                ));
            }

            /**Return range that allows iterate all immediate childrens of specified prefix*/
            auto sibling_range(const atom_string_t& key) const
            {
                return OP::flur::make_lazy_range(make_mixed_sequence_factory(
                    std::const_pointer_cast<const this_t>(this->shared_from_this()),
                    typename Ingredient<this_t>::Find(key),
                    typename Ingredient<this_t>::SiblingNext{}
                ));

            }

            /** Utilize feature of Trie where all entries below the single prefix are lexicographicaly ordered.
            * This range provide access to ordered sequence of suffixes. In simplified view you can think
            * about it as a cutting right part of each string from trie
            * @param begin - specifies begin iterator of prefix that will be cut-off
            *  @param aend - specifies end iterator of prefix that will be cut-off
            */
            /*template <class Atom>
            auto section_range(Atom begin, Atom aend) const
            {
                atom_string_t prefix{ begin, aend };
                return section_range(std::move(prefix));
            }
            template <class AtomString>
            auto section_range(AtomString prefix) const
            {
                return prefixed_range(prefix) >>
                flur::MappingFactory<;
                using result_t = TrieSectionAdapter<typename decltype(source)::element_type>;
                return ordered_range_ptr( new result_t(source, std::move(prefix)) );
            } */

            value_type value_of(position_t pos) const
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto node = view<node_t>(*_topology_ptr, pos.address());
                if (pos.key() < dim_t{ 256 })
                    return node->get_value(*_topology_ptr, (atom_t)pos.key());
                op_g.rollback();
                throw std::invalid_argument("position has no value associated");
            }

            /**
            *   @return pair, where
            * \li `first` - has child
            * \li `second` - has value (is terminal)
            */
            std::pair<bool, bool> get_presence(position_t position) const
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
                auto on_update = [&op_g](iterator&) {
                    op_g.rollback(); //do nothing on update TODO: check case of nested transaction if smthng is destroyed
                };
                auto result = std::make_pair(end(), true);
                result.second = !upsert_impl(result.first, begin, aend, value_assigner, on_update);

                return result;
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
                auto result = std::make_pair(of_prefix, true);
                alter_navigation(result.first);
                result.second = !upsert_impl(
                    result.first, begin, aend, value_assigner, on_update);
                return result;
            }

            template <class AtomContainer>
            std::pair<iterator, bool> prefixed_insert(iterator& of_prefix, const AtomContainer& container, Payload payload)
            {
                return prefixed_insert(of_prefix, std::begin(container), std::end(container), payload);
            }

            /**
            *   @return number of items updated (1 or 0)
            */
            size_t update(iterator& pos, Payload&& value)
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);
                auto sync_res = sync_iterator(pos);
                if (!std::get<bool>(sync_iterator(pos)) || pos.is_end())
                { //no entry for previous iterator
                    return 0;
                }
                const auto& back = pos.rat();
                assert(all_set(back.terminality(), Terminality::term_has_data));

                auto wr_node = accessor<node_t>(*_topology_ptr, back.address());
                wr_node->set_value(*_topology_ptr, (atom_t)back.key(), std::move(value));
                pos.rat(node_version(wr_node->_version));
                const auto this_ver = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ()
                    .increase_version() // version of trie
                    .current_version()
                    ;
                pos._version = this_ver;
                return 1;
            }

            /**
            * Update or insert value specified by key that formed as `[begin, end)`.
            * @param value - payload to be assigned anyway
            * @return pair of iterator and boolean indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key. Boolean indicator is false when
            *       item already exists and true when it was inserted
            */
            template <class AtomIterator>
            std::pair<iterator, bool> upsert(AtomIterator begin, AtomIterator aend, Payload&& value)
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
            * @param value - payload to be assigned or updated
            * @return pair of iterator and boolean indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to the already existing key. Boolean indicator is
            *       false when item already exists and true when it was inserted. When result equals to
            *       (#end(), false) it means caller provided empty string, nothing is updated.
            */
            template <class AtomIterator>
            std::pair<iterator, bool> prefixed_upsert(iterator& of_prefix, AtomIterator begin, AtomIterator aend, Payload value)
            {
                if (begin == aend)
                    return std::make_pair(end(), false); //empty string is not operatable
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true/*commit automatically*/);
                auto sync_res = sync_iterator(of_prefix);
                if (!std::get<bool>(sync_res))
                { //no entry for previous iterator, just insert
                    return insert(std::get<atom_string_t>(sync_res).append(begin, aend), std::move(value));
                }
                auto value_assigner = [&]() {
                    return std::move(value);
                };
                auto on_update = [&](iterator& pos) {
                    const auto& back = pos.rat();
                    assert(all_set(back.terminality(), Terminality::term_has_data));
                    //access values for write
                    auto wr_node = accessor<node_t>(*_topology_ptr, back.address());
                    wr_node->set_value(*_topology_ptr, (atom_t)back.key(), std::move(value));
                };
                auto result = std::make_pair(of_prefix, true);
                alter_navigation(result.first);
                result.second = !upsert_impl(
                    result.first, begin, aend, value_assigner, on_update);
                return result;
            }

            template <class AtomContainer>
            std::pair<iterator, bool> prefixed_upsert(iterator& prefix, const AtomContainer& container, Payload value)
            {
                return prefixed_upsert(prefix, std::begin(container), std::end(container), value);
            }

            iterator erase(iterator& pos, size_t* count = nullptr)
            {
                if (count) { *count = 0; }
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true);

                if (!std::get<bool>(sync_iterator(pos)) || pos.is_end())
                    return end();
                auto result{ pos };
                ++result;
                const auto root_addr = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().get_root_addr();
                bool erase_child_and_exit = false; //flag mean stop iteration
                for (bool first = true; pos.node_count(); pos.pop(), first = false)
                {
                    const auto& back = pos.rat();
                    auto wr_node = accessor<node_t>(*_topology_ptr, back.address());
                    if (erase_child_and_exit)
                    {//previous node may leave reference to child
                        wr_node->remove_child(*_topology_ptr, static_cast<atom_t>(back.key()));
                        pos.rat(
                            terminality_and(~Terminality::term_has_child),
                            node_version(wr_node->_version)
                            );
                    }

                    if (!wr_node->erase(*_topology_ptr, static_cast<atom_t>(back.key()), first))
                    { //don't continue if exists child node
                        pos.rat(
                            terminality_and(~Terminality::term_has_data),
                            node_version(wr_node->_version));
                        break;
                    }
                    //remove node if not a root
                    if (back.address() != root_addr)
                    {
                        remove_node(back.address());
                        erase_child_and_exit = true;
                    }
                }
                _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ()
                    .increase_count(-1) //number of terminals
                    .increase_version() // version of trie
                    ;
                if (count) { *count = 1; }
                return result;
            }

            /**Simplfied form of erase(iterator&, size_t*)*/
            iterator erase(iterator&& pos)
            {
                iterator snapshot = std::move(pos);
                return erase(snapshot, nullptr);
            }

            /**
            * Erase every entries that begins with prfix specified by iterator. If trie contains entry excatly matched to
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
                const auto& rat = prefix.rat();
                if (is_not_set(rat.terminality(), Terminality::term_has_child))
                { //no child below, so skip any erase
                    size_t counter = 0;
                    prefix = erase(prefix, &counter);
                    return counter;
                }
                auto parent_wr_node = accessor<node_t>(*_topology_ptr, rat.address());
                auto back = classify_back(parent_wr_node, prefix);
                std::stack<FarAddress> to_process;
                to_process.push(std::get<FarAddress>(back));
                size_t erased_terminals = 0;
                while (!to_process.empty())
                {
                    auto node_addr = to_process.top();
                    to_process.pop();

                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    erased_terminals += wr_node->erase_interior(*_topology_ptr, to_process);
                    remove_node(node_addr);
                }
                parent_wr_node->remove_child(*_topology_ptr, static_cast<atom_t>(rat.key()));
                auto& residence = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ()
                    .increase_count(-static_cast<std::make_signed_t<size_t>>(erased_terminals)) //number of terminals
                    .increase_version() // version of trie
                    ;
                prefix._version = residence.current_version();
                prefix.rat(
                    node_version(parent_wr_node->_version),
                    terminality_and(~Terminality::term_has_child));
                return erased_terminals;
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
                try {
                    return f(*this);
                }
                catch (...) {
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
            auto apply(F const& f) const -> typename std::result_of<F(const this_t&)>::type
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); //autocommit on return
                return f(*this);
            }

        private:
            typedef FixedSizeMemoryManager<node_t, initial_node_count> node_manager_t;
            using topology_t =
                SegmentTopology<
                TrieResidence,
                node_manager_t,
                HeapManagerSlot/*Memory manager must go last*/>;
            std::unique_ptr<topology_t> _topology_ptr;

        private:
            Trie(std::shared_ptr<TSegmentManager>& segments) noexcept
                : _topology_ptr{ std::make_unique<topology_t>(segments) }
            {

            }
            /** Create new node with default requirements.
            * It is assumed that exists outer transaction scope.
            */
            FarAddress new_node(size_t level = 1)
            {
                TrieOptions options; //@! temp - just default impl
                auto node_addr = _topology_ptr->OP_TEMPL_METH(slot) < node_manager_t > ().allocate(
                    options.init_node_size(level)
                );
                auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                wr_node->create_interior(*_topology_ptr);
                auto& res = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ();
                res.increase_nodes_allocated(+1);
                return node_addr;
            }

            void remove_node(FarAddress addr)
            {
                _topology_ptr->OP_TEMPL_METH(slot) < node_manager_t > ().deallocate(addr);
                auto& res = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ();
                res.increase_nodes_allocated(-1);
            }

            /**
            *  On insert to `break_position` stem may contain chain to split. This method breaks the chain
            *  and place the rest to a new children node.
            * @return address of new node
            */
            FarAddress diversificate(node_t& wr_node, iterator& break_position)
            {
                const auto& back = break_position.rat();
                //create new node to place result
                FarAddress new_node_addr = new_node(break_position.deep());
                atom_t key = static_cast<atom_t>(back.key());
                dim_t in_stem_pos = back.stem_size();
                wr_node.move_to(*_topology_ptr, key, in_stem_pos, new_node_addr);
                //wr_node.set_child(*_topology_ptr, key, new_node_addr);
                break_position.rat(terminality_or(Terminality::term_has_child));

                return new_node_addr;
            }
            /**Place string to node without any additional checks*/
            template <class FValueAssigner>
            void unconditional_insert(iterator& result, FValueAssigner fassign)
            {
                const auto& back = result.rat();
                assert(back.key() < 256);
                atom_t key = static_cast<atom_t>(back.key());
                auto wr_node = accessor<node_t>(*_topology_ptr, back.address());
                assert(back.stem_size() != dim_nil_c );
                wr_node->insert(
                    *_topology_ptr, key, 
                    result._prefix.end() - back.stem_size(), result._prefix.end(),
                    fassign);
                result.rat(
                    node_version(wr_node->_version),
                    terminality_or(Terminality::term_has_data)
                );
                // THIS impl version never meet condition begin == end
                //if (begin != end) //not fully fit to this node
                //{
                //    //some suffix have to be accomodated yet
                //    node_addr = new_node(result.deep());
                //    wr_node->set_child(*_topology_ptr, key, node_addr);
                //}
                _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ()
                    .increase_count(+1) //number of terminals
                    .increase_version() // version of trie
                    ;
            }

            template <class AtomIterator>
            StemCompareResult mismatch(iterator& iter, AtomIterator& begin, AtomIterator end) const
            {
                using node_data_t = typename node_t::NodeData;
                //auto pref_res = common_prefix(begin, end, iter);
                StemCompareResult mismatch_result = StemCompareResult::equals;
                for (FarAddress node_addr = iter.rat().address(); 
                    begin != end 
                    && !node_addr.is_nil()
                    && any_of(mismatch_result, StemCompareResult::equals, StemCompareResult::stem_end);)
                {
                    auto node =
                        view<node_t>(*_topology_ptr, node_addr);
                    atom_t step_key = *begin++;
                    bool has_child = node->has_child(step_key);
                    bool has_value = node->has_value(step_key);

                    iter.rat(
                        //address(node_addr),
                        key(step_key),
                        stem_size(dim_nil_c), //no stem info yet
                        terminality(
                            (has_value ? Terminality::term_has_data : Terminality::term_no)
                            | (has_child ? Terminality::term_has_child : Terminality::term_no)),
                        node_version(node->_version)
                    );

                    if (!has_value)
                    {//mismatch reached
                        if(!has_child )
                            return StemCompareResult::no_entry;
                        if (begin == end)
                        {
                            iter.rat(stem_size(0));
                            return StemCompareResult::string_end;
                        }
                    }
                    assert(node->magic_word_c == 0x55AA);
                    mismatch_result = node->rawc(*_topology_ptr, step_key,
                        [&](const node_data_t& node_data) -> StemCompareResult {
                            node_addr = node_data._child;//if exists discover next child node
                            StemCompareResult stem_matches = StemCompareResult::equals;
                            if (!node_data._stem.is_nil())
                            { //stem exists, append to out iterator
                                StringMemoryManager smm(*_topology_ptr);
                                auto result_stem_size = static_cast<dim_t>(
                                    smm.get(node_data._stem, [&](atom_t c) -> bool {
                                        if (begin == end)
                                        {
                                            stem_matches = StemCompareResult::string_end;
                                            return false; //stop comparison
                                        }
                                        else if (c != *begin)
                                        {
                                            stem_matches = StemCompareResult::unequals;
                                            return false; //stop comparison
                                        }
                                        iter._prefix.append(1, *begin++);
                                        return true;//continue comparison
                                        }));
                                iter.rat(stem_size(result_stem_size));
                            }
                            else
                                iter.rat(stem_size(0));
                            if (stem_matches == StemCompareResult::equals)
                            {
                                if(begin != end)
                                    stem_matches = StemCompareResult::stem_end;
                                else if (!has_value)
                                {
                                    stem_matches =/* has_child ?
                                        StemCompareResult::no_entry :*/ StemCompareResult::string_end;
                                }
                                //otherwise we have real `equals`
                            }
                            return stem_matches;
                        }
                    );
                    if (//none_of(mismatch_result, 
                        //StemCompareResult::no_entry, StemCompareResult::string_end, StemCompareResult::equals, StemCompareResult::unequals)
                        mismatch_result == StemCompareResult::stem_end && has_child)
                    { //next iteration with new node address
                        //mismatch_result == StemCompareResult::stem_end 
                        iter.push(address(node_addr));
                        continue;
                    }
                }
                //there since both iterators reached the end
                return mismatch_result;
            }
            /**
            * it is always about to insert, since if need to update 
            * smthng - then `this->mismatch` should return `equals`
            */
            template <class FValueEval>
            void insert_mismatch_string_end(
                WritableAccess<node_t>& wr_node,
                iterator& iter, FValueEval f_value_eval)
            {
                //assert(!wr_node->has_value(step_key));
                auto back = iter.rat();//no ref, copy!
                assert(back.key() <= std::numeric_limits<atom_t>::max());
                atom_t step_key = static_cast<atom_t>(back.key());

                wr_node->rawc(*_topology_ptr, step_key, [&](auto& src_entry){
                    if (src_entry._stem.is_nil())
                        return;
                    auto new_node_addr = new_node(iter.node_count() + 1);
                    auto target_node = accessor<node_t>(*_topology_ptr, new_node_addr);
                    wr_node->move_to(*_topology_ptr, step_key, back.stem_size(), target_node);
                    iter.rat(
                        terminality_or(Terminality::term_has_child)
                    );
                });
                assert(!wr_node->has_value(step_key));
                //wr_node->_value_presence.set(step_key);
                wr_node->set_value(*_topology_ptr, step_key, f_value_eval());
                iter.rat(
                    terminality_or(Terminality::term_has_data),
                    node_version(wr_node->_version)
                    );

                _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ()
                    .increase_count(+1) //number of terminals
                    .increase_version() // version of trie
                    ;
            }
            
            /**
            * 
            * \return true if value already exists, false if new value has been added
            */
            template <class AtomIterator, class FValueEval, class FOnUpdate>
            bool upsert_impl(
                iterator& iter, AtomIterator begin, AtomIterator end, FValueEval f_value_eval, FOnUpdate f_on_update)
            {
                if (iter.is_end())
                { //start from root node
                    iter.push(
                        address(
                            _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().get_root_addr())
                    );
                }
                
                StemCompareResult mismatch_result = mismatch(iter, begin, end);
                
                if (begin == end && mismatch_result == StemCompareResult::equals)//full match
                {
                    f_on_update(iter);
                    return true;
                }

                if (mismatch_result == StemCompareResult::no_entry)
                { //no entry in the current node, drain stem if exists
                    iter.update_stem(begin, end);
                }
                else //entry in this node already exists, hence we need new node
                {
                    auto back = iter.rat();//no reference
                    atom_t step_key = static_cast<atom_t>(back.key());
                    auto wr_node = accessor<node_t>(*_topology_ptr, back.address());

                    if (mismatch_result == StemCompareResult::string_end)
                    {
                        insert_mismatch_string_end(wr_node, iter, std::move(f_value_eval));
                        return false;
                    }

                    auto new_node_addr = new_node(iter.node_count()+1);
                    if (mismatch_result == StemCompareResult::stem_end)
                    {
                        assert(is_not_set(back._terminality, Terminality::term_has_child));
                        wr_node->set_child(
                            *_topology_ptr, step_key, new_node_addr);
                        iter.rat(node_version(wr_node->_version));
                        iter.push(
                            key(*begin++),
                            address(new_node_addr)
                        );
                        iter.update_stem(begin, end);
                    }
                    else //stem is fully processed
                    {
                        //wr_node->set_child(*_topology_ptr, step_key, new_node_addr);
                        //iter.rat(
                        //    terminality_or(Terminality::term_has_child),
                        //    node_version(wr_node->_version));
                        auto target_node = accessor<node_t>(*_topology_ptr, new_node_addr);

                        wr_node->move_to(*_topology_ptr, step_key, back.stem_size(), target_node);
                        iter.rat(
                            terminality_or(Terminality::term_has_child),
                            node_version(wr_node->_version));
                        atom_t k = *begin++;
                        iter.push(
                            key(k),
                            address(new_node_addr)
                        );
                        iter.update_stem(begin, end);
                    }
                }
                unconditional_insert(iter, f_value_eval);
                const auto this_ver = this->version();
                iter._version = this_ver;
                return false;//brand new entry
            }
            /**Insert or update value associated with specified key. The value is passed as functor evaluated on demand.
            * @param start_from - iterator to start, note it MUST be already synced. At exit
            * @return pair of iterator and success indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key
            */
            template <class AtomIterator, class FValueEval, class FOnUpdate>
            void upsert_impl_ols(std::pair<iterator, bool>& result, AtomIterator begin, AtomIterator end, FValueEval f_value_eval, FOnUpdate f_on_update)
            {
                auto& iter = result.first;
                auto origin_begin = begin;
                auto pref_res = common_prefix(begin, end, iter);
                switch (pref_res.compare_result)
                {
                case StemCompareResult::no_entry:
                {
                    unconditional_insert(iter, f_value_eval);
                    return /*result*/;
                }
                case StemCompareResult::stem_end: //stem is over, just follow downstair to next node
                {
                    assert(!iter.is_end());
                    auto& back = iter.back();
                    FarAddress node_addr = back.address();
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);

                    node_addr = new_node(iter.node_count());
                    wr_node->set_child(*_topology_ptr, static_cast<atom_t>(back.key()), node_addr);
                    unconditional_insert(iter, f_value_eval);
                    return /*result*/;
                }
                case StemCompareResult::unequals:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    auto& back = iter.back();
                    FarAddress node_addr = back.address();
                    //entry should exists
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    if (origin_begin != begin)
                    { //need split stem 
                        node_addr =
                            diversificate(*wr_node, iter);
                    }
                    else
                    {// nothing to diversificate
                        assert(pref_res.child_node.is_nil());
                        node_addr = new_node(iter.node_count());
                        wr_node->set_child(*_topology_ptr, (atom_t)back.key(), node_addr);
                    }
                    unconditional_insert(iter, f_value_eval);
                    return /*result*/;
                }
                case StemCompareResult::equals:
                    if (iter.back()._terminality & Terminality::term_has_data)
                    {
                        f_on_update(iter);
                        result.second = false;
                        return; //dupplicate found
                    }
                    // no break there
                    [[fallthrough]];
                case StemCompareResult::string_end:
                {
                    assert(!iter.is_end()); //even if unequal raised in root node it must lead to some stem
                    //case about non-existing item
                    _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ()
                        .increase_count(1) //number of terminals
                        .increase_version() // version of trie
                        ;

                    auto& back = iter.back();
                    FarAddress node_addr = back.address();
                    auto wr_node = accessor<node_t>(*_topology_ptr, node_addr);
                    wr_node->raw(*_topology_ptr, static_cast<atom_t>(back.key()),
                        [&](auto& node_data) {
                            if (!node_data._stem.is_nil())
                            {
                                StringMemoryManager smm(*_topology_ptr);
                                auto size = smm.size(node_data._stem);

                                //empty iterator should not be diversificated, just set value
                                //terminal is not there, otherwise it would be StemCompareResult::equals
                                if (back.deep() == size)
                                {
                                    //it is allowed only to have child, if 'has_data' set - then `common_prefix` works wrong
                                    assert(!(back.terminality() & Terminality::term_has_data));
                                    //don't do anything, wait until wr_node->set_value
                                }
                                else
                                {
                                    diversificate(*wr_node, iter);
                                }
                            }

                            wr_node->set_raw_value(
                                *_topology_ptr, static_cast<atom_t>(back.key()),
                                node_data, std::forward<Payload>(f_value_eval()));
                            back._version = wr_node->_version;
                        });

                    iter.back()._terminality |= Terminality::term_has_data;
                    return /*result*/;
                }
                default:
                    assert(false);
                    result.second = false;
                    return /*result*/; //fail stub
                }
            }
            /** Prepares `result_iter` to further navigation deep, if it is empty 
            * method prepares navigation from root node
            */
            bool navigation_mode(iterator& result_iter) const
            {
                FarAddress next_address;
                if (result_iter.is_end())
                { //start from root node
                    next_address =
                        _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().get_root_addr();
                }
                else
                {
                    if (!all_set(result_iter.rat().terminality(), Terminality::term_has_child))
                        return false;//no way down
                    const auto& back = result_iter.rat();
                    next_address = view<node_t>(
                        *_topology_ptr, back.address())
                        ->get_child(*_topology_ptr, static_cast<atom_t>(back.key()))
                        ;
                }
                result_iter.push(
                    address(next_address)
                );
                return true;
            }
            
            /**
            *  Prepare iterator for navigation for trie altering (insert, upsert).
            * Method checks if existing iterator capable for navigation (not the end, 
            *   otherwise root node used) 
            * and allows insertion (contains child node at pointed position - otherwise
            * new empty node added)
            */
            void alter_navigation(iterator& result_iter)
            {
                if (!navigation_mode(result_iter))
                {//no way down, so need create children empty node
                    const auto& back = result_iter.rat();
                    assert(back.key() < dim_t{ 256 });
                    auto addr = new_node(result_iter.node_count());
                    accessor<node_t>(*_topology_ptr, back.address())
                        ->set_child(*_topology_ptr, static_cast<atom_t>(back.key()), addr);
                    result_iter.push(
                        address(addr)
                    );
                }
            }
            /**Return not fully valid iterator that matches common part specified by [begin, end)*/
            template <class Atom>
            typename node_t::nav_result_t common_prefix(Atom& begin, Atom end, iterator& result_iter) const
            {
                typename node_t::nav_result_t retval{};
                if (begin == end || !navigation_mode(result_iter))
                { //nothing to consider
                    return retval;
                }

                retval.compare_result =
                    mismatch(result_iter, begin, end);
                retval.child_node = result_iter.rat().address();
                return retval;
                //if (end == begin)
                //{
                //    return retval;
                //}
                //FarAddress node_addr;
                //if (!result_iter.is_end()) //discover from previously positioned iterator
                //{
                //    //there is great assumption that result_iter's deep points to full stem
                //    auto cls = classify_back(result_iter);
                //    if (!std::get<1>(cls)) //no way down
                //    {
                //        return retval;//end()
                //    }
                //    node_addr = std::get<FarAddress>(cls);
                //}
                //else
                //{ //start from root node
                //    node_addr = _topology_ptr->OP_TEMPL_METH(slot) < TrieResidence > ().get_root_addr();
                //}
                //for (;;)
                //{
                //    auto node =
                //        view<node_t>(*_topology_ptr, node_addr);

                //    atom_t key = *begin;
                //    retval =
                //        node->navigate_over(*_topology_ptr, begin, end, node_addr, result_iter);

                //    // all cases excluding StemCompareResult::stem_end mean that need return current iterator state
                //    if (StemCompareResult::stem_end == retval.compare_result)
                //    {
                //        node_addr = retval.child_node;
                //        if (node_addr.address == SegmentDef::far_null_c)
                //        {  //no way down, 
                //            return retval; //@! it is the prefix that is not satisfied lower_bound
                //        }
                //    }
                //    else if (StemCompareResult::no_entry == retval.compare_result)
                //    {
                //        retval.child_node = node_addr;
                //        return retval;
                //    }
                //    else
                //        break;

                //} //for(;;)
                //return retval;
            }
            template <class FChildLocator>
            iterator position_child(iterator& of_this, FChildLocator locator) const
            {
                if (!std::get<bool>(sync_iterator(of_this)) || of_this == end() ||
                    is_not_set(of_this.rat().terminality(), Terminality::term_has_child))
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
                } while (is_not_set(result.rat().terminality(), Terminality::term_has_data));
            }
            /**
            * @return true when result matches exactly to specified string [begin, aend)
            */
            template <class Atom>
            bool lower_bound_impl(Atom& begin, Atom aend, iterator& prefix) const
            {
                //auto nav_res = common_prefix(begin, aend, prefix);
                if (begin == aend || !navigation_mode(prefix))
                {
                    prefix = end();
                    return false;
                }
                auto unmatch_result = mismatch(prefix, begin, aend);

                switch (unmatch_result)
                {
                case StemCompareResult::equals: //exact match
                    return true;
                case StemCompareResult::unequals:
                {
                    if (!prefix.is_end())
                    {
                        _next(true, prefix);
                    }
                    return false; //not an exact match
                }
                case StemCompareResult::no_entry:
                {
                    auto back = prefix.rat(stem_size(0));//not a reference
                    auto lres = load_iterator(
                        back.address(), prefix,
                        [&](ReadonlyAccess<node_t>& ro_node) 
                        { //on absence of entry just try find bigger in the same node
                            return ro_node->next_or_this(static_cast<atom_t>(back.key()));
                        },
                        &iterator::update_back);

                    if (!std::get<0>(lres))
                    {
                        prefix.pop();
                        _next(false/*no reason to enter deep*/, prefix);
                        return false; //no an exact match
                    }

                    if (is_not_set(prefix.rat().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(back.address(), prefix);
                    }
                    return false; //not an exact match
                }
                case StemCompareResult::string_end:
                {
                    auto lres = load_iterator(//need reload since after common_prefix may not be complete
                        prefix.rat().address(), prefix,
                        [&](ReadonlyAccess<node_t>& ro_node) {
                            return make_nullable(prefix.rat().key());
                        },
                        &iterator::update_back);
                    assert(std::get<bool>(lres));
                    if (is_not_set(prefix.rat().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(std::get<FarAddress>(lres), prefix);
                    }
                    return false;//not an exact match
                }
                case StemCompareResult::stem_end:
                    break; //just follow

                }
                //when: StemCompareResult::stem_end || StemCompareResult::string_end ||
                //   ( StemCompareResult::unequals && !iter.is_end())
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
            std::pair<bool, atom_string_t> sync_iterator(iterator& it) const
            {
                const auto this_ver = this->version();
                std::pair<bool, atom_string_t> result = { true, {} };
                if (this_ver == it.version()) //no sync is needed
                    return result;
                it._version = this_ver;
                dim_t order = 0;
                size_t prefix_length = 0;
                //take each node of iterator and check against current version of real node
                for (auto i : it._position_stack)
                {
                    auto node = view<node_t>(*_topology_ptr, i.address());
                    if (node->_hash_table.is_nil())
                    {//may be iterator so old, so node been removed
                        std::get<0>(result) = false;
                        it = end();
                        return result;
                    }
                    if (node->_version != i.version())
                    {
                        auto& repeat_search_key = std::get<1>(result);
                        repeat_search_key = it.key(); //need make copy since next instructions corrupt the iterator
                        it._prefix.resize(prefix_length);
                        it._position_stack.erase(it._position_stack.begin() + order, it._position_stack.end()); //cut stack
                        auto suffix_begin = repeat_search_key.begin() + prefix_length //cut prefix str
                            , suffix_end = repeat_search_key.end()
                            ;
                        if (!lower_bound_impl(suffix_begin, suffix_end, it))
                        {
                            std::get<0>(result) = false;
                            it = end();
                            return result;
                        }
                        return result; //still get<0> == true
                    }
                    assert(i.stem_size() != dim_nil_c);
                    prefix_length += i.stem_size() + 1;
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
                auto key_val = dest._position_stack.back().key();
                assert(key_val < dim_t{ 256 });
                auto key = (atom_t)key_val;
                FarAddress child_addr = {};
                if (ro_node->has_child(key))
                {
                    child_addr =
                        ro_node->get_child(*_topology_ptr, key);
                }
                return std::make_tuple(
                    ro_node->has_value(key),
                    ro_node->has_child(key),
                    child_addr);
            }

            std::tuple<bool, bool, FarAddress> classify_back(const iterator& dest) const
            {
                auto ro_node = view<node_t>(*_topology_ptr,
                    dest._position_stack.back().address());
                return classify_back(ro_node, dest);
            }
            template <class TNode, class FIteratorUpdate>
            void restore_iterator_from_pos(TNode& node, atom_t key, iterator& dest, FIteratorUpdate update_method)
            {
                return node->rawc(*_topology_ptr, key,
                    [&](const auto& node_data) {
                        if (!node_data._stem.is_nil())
                        {//stem exists and must be placed to iterator
                            StringMemoryManager smm(*_topology_ptr);
                            atom_string_t buffer;
                            smm.get(node_data._stem, std::back_inserter(buffer));
                            (dest.*update_method)(std::move(root_pos),
                                buffer.data(), buffer.data() + buffer.size());
                        }
                        else //no stem
                        {
                            (dest.*iterator_update)(std::move(root_pos), nullptr, nullptr);
                        }
                        dest.rat(
                            terminality(
                                (ro_node->has_value(pos.second) ? Terminality::term_has_data : Terminality::term_no)
                                | (ro_node->has_child(pos.second) ? Terminality::term_has_child : Terminality::term_no)
                            )
                        );
                        return std::make_tuple(true, node_data._child);
                    });

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
                if (!pos)
                { //no first
                    return std::make_tuple(false, FarAddress());
                }

                position_t root_pos(
                    address(node_addr),
                    key(pos.second),
                    node_version(ro_node->_version)
                );
                return ro_node->rawc(*_topology_ptr, pos.second,
                    [&](const auto& node_data) {
                        if (!node_data._stem.is_nil())
                        {//if stem exists should be placed to iterator
                            StringMemoryManager smm(*_topology_ptr);
                            atom_string_t buffer;
                            smm.get(node_data._stem, std::back_inserter(buffer));
                            (dest.*iterator_update)(std::move(root_pos),
                                buffer.data(), buffer.data() + buffer.size());
                        }
                        else //no stem
                        {
                            (dest.*iterator_update)(std::move(root_pos), nullptr, nullptr);
                        }
                        dest.rat(
                            terminality(
                                (ro_node->has_value(pos.second) ? Terminality::term_has_data : Terminality::term_no)
                                | (ro_node->has_child(pos.second) ? Terminality::term_has_child : Terminality::term_no)
                            )
                        );
                        return std::make_tuple(true, node_data._child);
                    });

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
            void _next(bool way_down, iterator& i) const
            {
                while (!i.is_end())
                {
                    const auto& back = i.rat();
                    //try enter deep
                    if (way_down && all_set(back.terminality(), Terminality::term_has_child))
                    {   //get address of child
                        auto ro_node = view<node_t>(*_topology_ptr, back.address());
                        auto child_addr = ro_node->get_child(
                            *_topology_ptr, static_cast<atom_t>(back.key()));
                        enter_deep_until_terminal(child_addr, i);
                        return;
                    }
                    //try navigate right from current position
                    auto lres = load_iterator(i.rat().address(), i,
                        [&i](ReadonlyAccess<node_t>& ro_node)
                        {
                            //don't optimize `i.rat` since i may change
                            return ro_node->next((atom_t)i.rat().key());
                        },
                        &iterator::update_back);
                    if (std::get<bool>(lres))
                    { //navigation right succeeded
                        if (all_set(i.rat().terminality(), Terminality::term_has_data))
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
                } while (is_not_set(i.rat().terminality(), Terminality::term_has_data));
            }
        };

    } //ns:trie
}//ns:OP

#endif //_OP_TRIE_TRIE__H_
