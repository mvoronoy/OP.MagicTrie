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

#include <op/common/astr.h>
#include <op/trie/Containers.h>
#include <op/vtm/FixedSizeMemoryManager.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/HeapManager.h>

#include <op/trie/TrieNode.h>
#include <op/trie/TrieIterator.h>
#include <op/trie/TrieResidence.h>
#include <op/trie/StoreConverter.h>
#include <op/trie/MixedAdapter.h>

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


        template <class TSegmentManager, class TPayloadManager, std::uint32_t initial_node_count = 512>
        struct Trie : public std::enable_shared_from_this< Trie<TSegmentManager, TPayloadManager, initial_node_count> >
        {
        public:
            using atom_t = OP::common::atom_t;
            using trie_t = Trie<TSegmentManager, TPayloadManager, initial_node_count>;
            using payload_manager_t = TPayloadManager;
            using payload_t = typename payload_manager_t::payload_t;
            using this_t = trie_t;
            using iterator = TrieIterator<this_t>;
            using value_type = typename payload_manager_t::source_payload_t;
            using node_t = TrieNode<payload_manager_t>;
            using position_t = TriePosition;
            using key_t = OP::common::atom_string_t;
            using insert_result_t = std::pair<iterator, bool>;
            using storage_converter_t = typename payload_manager_t::storage_converter_t;

            virtual ~Trie()
            {
            }

            static std::shared_ptr<Trie> create_new(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                //create new file
                auto new_trie = std::shared_ptr<this_t>(new this_t(segment_manager));
                //make root for trie
                OP::vtm::TransactionGuard op_g(segment_manager->begin_transaction()); //invoke begin/end write-op
                
                // never(!) place `->new_node` and `.update([](){})` to single lambda
                // otherwise creates different version writable_block
                new_trie->_root = new_trie->new_node(0);
                
                new_trie->_topology->template slot<TrieResidence> ()
                    .update([root_addr = new_trie->_root](auto& header){
                        header._root = root_addr;
                    });

                op_g.commit();
                return new_trie;
            }
            
            static std::shared_ptr<Trie> open(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                auto existing_trie = std::shared_ptr<this_t>(new this_t(segment_manager));
                auto header =
                    existing_trie->_topology->template slot<TrieResidence> ()
                    .get_header();
                existing_trie->_version = header._version;
                existing_trie->_root = header._root;
                return existing_trie;
            }

            TSegmentManager& segment_manager()
            {
                return static_cast<TSegmentManager&>(
                    OP::trie::resolve_segment_manager(*_topology));
            }
            
            /**Total number of items*/
            std::uint64_t size() const
            {
                auto h = _topology->template slot<TrieResidence>()
                    .get_header();
                return h._count;
            }

            node_version_t version() const
            {
                auto h = _topology->template slot<TrieResidence>()
                    .get_header();
                return this->_version;
            }
            
            /**Number of allocated nodes*/
            std::uint64_t nodes_count()
            {
                auto h = _topology->template slot<TrieResidence>().get_header();
                return h._nodes_allocated;
            }

            iterator begin() const
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), false); //place all RO operations to atomic scope
                auto next_addr = _root;
                iterator i(this);
                bool ok = true;
                auto locator = [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); };
                do{
                    std::tie(ok, next_addr) = load_iterator(
                        next_addr, i, locator, &iterator::emplace);
                    if (!ok)
                    {
                        assert(i.node_count() == 0);//allowed !ok only for zero level
                        return end();
                    }
                } while (is_not_set(i.rat().terminality(), Terminality::term_has_data));
                return i;
            }

            iterator end() const
            {
                return iterator(this, typename iterator::end_marker_t{});
            }
            /** check if iterator is not end() */
            bool in_range(const iterator& check) const
            {
                return check != end();
            }
            /** Shift iterator to the next (lexicographically bigger) position */
            void next(iterator& i) const
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                if (sync_iterator(i))
                {
                    _next(i);
                }
            }
            /**
            * Iterate next item that is not prefixed by current (`i`). In other words select right of this or parent. In compare with
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope

                if (!sync_iterator(i))
                    return;
                while (!i.is_end())
                {
                    auto [ok, child] = load_iterator(
                        i.rat().address(), i,
                        [&i](ReadonlyAccess<node_t>& ro_node)
                        {
                            //don't optimize i.rat() since iterator updated inside `load_iterator`
                            return ro_node->next((atom_t)i.rat().key());
                        },
                        &iterator::update_back);
                    if (ok)
                    { //navigation right succeeded
                        if (all_set(i.rat().terminality(), Terminality::term_has_data))
                        {//i already points to the correct entry
                            return;
                        }
                        enter_deep_until_terminal(child, i, 
                            [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); });
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope

                if (!sync_iterator(i) || key.empty())
                    return;
                auto [mis_it, mis_key] = std::mismatch(
                    i.key().begin(), i.key().end(), key.begin(), key.end());
                while (mis_it < i.key().end())
                    i.pop();
                if (mis_key != key.end())
                {
                    lower_bound_impl(mis_key, key.end(), i);
                }
                else
                {
                    auto kbeg = key.begin();
                    auto iend = end();
                    lower_bound_impl(kbeg, key.end(), iend);
                }
            }

            /** return first entry that contains prefix specified by string [begin, aend)
            *   @param begin - first symbol of string to lookup
            *   @param aend - end of string to lookup
            *   \tparam IterateAtom iterator of string
            */
            template <class IterateAtom>
            iterator prefixed_begin(IterateAtom begin, IterateAtom aend) const
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator i(this);
                auto nav = common_prefix(begin, aend, i);
                if (begin != aend) //no such prefix since begin wasn't exhausted
                    return end();
                auto i_beg = i;//, i_end = i;
                //find next position that doesn't matches to prefix
                //nothing to do for: if (nav.compare_result == StemCompareResult::equals //prefix fully matches to existing terminal
                if (nav == StemCompareResult::string_end) //key partially matches to some prefix
                { //correct string at the back of iterator
                    auto [ok, child] = load_iterator(i_beg.rat().address(), i_beg,
                        [&i](ReadonlyAccess<node_t>&) {
                            return NullableAtom{ i.rat().key() };
                        },
                        &iterator::update_back);
                    assert(ok);//tail must exists
                    if (is_not_set(i_beg.rat().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(child, i_beg,
                            [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); });
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), false); //place all RO operations to atomic scope
                OP::common::atom_string_t lookup_key;
                auto succeeded = sync_iterator(of_prefix, &lookup_key);
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), false); //place all RO operations to atomic scope
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator iter = end();
                if (sync_iterator(of_prefix))
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                iterator iter = of_prefix;
                if (sync_iterator(iter))
                {
                    if (lower_bound_impl(begin, aend, iter) && begin == aend)
                    {
                        return iter;
                    }
                    iter.clear();
                }
                return iter;
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), false); //place all RO operations to atomic scope
                auto b = std::begin(container);
                iterator iter = end();

                StemCompareResult nav_res = common_prefix(b, std::end(container), iter);

                return (nav_res == StemCompareResult::equals);
            }

            /**
            *   Get first child element resided below position specified by parameter `of_this`. Since all keys 
            *   in Trie are lexicographically
            *   ordered the return iterator indicate smallest immediate children
            *   For example having entries in trie: \code
            *       abc, abc.1, abc.123, abc.2, abc.3
            *   \endcode
            *   you may use `first_child(find("abc"))` to locate entry "abc.1" (compare with #lower_bound 
            *   that have to return "abc.123")
            *   \see last_child
            *   @return iterator to some child of_this or `end()` if no entry.
            */
            iterator first_child(iterator& of_this) const
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                return children_navigation(of_this,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); });
            }
            /**
            *   Get last child element resided below position specified by `of_this`. Since all keys 
            *   in Trie are lexicographically
            *   ordered the return iterator indicate largest immediate children
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //place all RO operations to atomic scope
                return children_navigation(of_this,
                    [](ReadonlyAccess<node_t>& ro_node) { return ro_node->last(); });
            }
            /**
            *   @return sequence-factory that embrace all records by pair `[ begin(), end() )` but in more effecient way.
            */
            auto range() const
            {
                return OP::flur::make_lazy_range(
                    TrieSequenceFactory<this_t>(this->shared_from_this()));
            }

            /**
            *   Construct a range that address all string started from string specified by [begin, aend)
            *   \param begin - first symbol of string to lookup
            *   \param aend - end of string to lookup
            *   \tparam IterateAtom iterator of string
            *   \return OP::flur lazy range that produces TrieSequence (sequence of Trie::iterator)
            */
            template <class IterateAtom>
            auto prefixed_range(IterateAtom begin, IterateAtom aend) const
            {
                OP::common::atom_string_t prefix(begin, aend);
                return prefixed_range(prefix);
            }

            /**
            *   Construct a range that contains all strings started with specified prefix
            * @param prefix is any string of bytes that supports std::begin / std::end iteration
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

            /**Return range that allows iterate all immediate children of specified prefix*/
            auto children_range(const iterator& of_this) const
            {
                return OP::flur::make_lazy_range(make_mixed_sequence_factory(
                    std::const_pointer_cast<const this_t>(this->shared_from_this()),
                    typename Ingredient<this_t>::ChildBegin{ of_this },
                    typename Ingredient<this_t>::ChildInRange{ StartWithPredicate(of_this.key()) },
                    typename Ingredient<this_t>::SiblingNext{}
                ));
            }

            /**Return range that allows iterate all immediate childrens of specified prefix
            * \tparam AtomContainer - any string-like character enumeration
            */
            template <class AtomContainer>
            auto children_range(const AtomContainer& of_key) const
            {
                return OP::flur::make_lazy_range(make_mixed_sequence_factory(
                    std::const_pointer_cast<const this_t>(this->shared_from_this()),
                    typename Ingredient<this_t>::template ChildOfKeyBegin<AtomContainer>{ of_key },
                    typename Ingredient<this_t>::ChildInRange{ StartWithPredicate(of_key) },
                    typename Ingredient<this_t>::SiblingNext{}
                ));
            }

            /**Return range that allows iterate all siblings of specified prefix*/
            auto sibling_range(const OP::common::atom_string_t& key) const
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

            auto value_of(position_t pos) const
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true);
                auto node = view<node_t>(*_topology, pos.address());
                if (pos.key() < dim_t{ 256 })
                {
                    return node->get_value(*_topology, (atom_t)pos.key(), [this](const auto& ref){
                        return OP::trie::store_converter::Storage<
                                typename payload_manager_t::source_payload_t
                            >::deserialize(*_topology, ref);
                    });
                }
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
            template <class AtomIterator, class FPayloadFactory>
            std::enable_if_t<std::is_invocable_v<FPayloadFactory>, insert_result_t>
                insert(AtomIterator& begin, AtomIterator aend, FPayloadFactory&& value_assigner)
            {
                if (begin == aend)
                    return std::make_pair(iterator(this), false); //empty string cannot be inserted

                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true);
                
                auto result = std::make_pair(end(), true);
                result.second = !insert_impl(
                    result.first, begin, aend, std::move(value_assigner));

                return result;
            }

            template <class StringLike, class FPayloadFactory>
            std::enable_if_t<std::is_invocable_v<FPayloadFactory>, insert_result_t>
                insert(const StringLike& str, FPayloadFactory&& value_assigner)
            {
                auto b = str.begin();
                return insert(b, str.end(), std::move(value_assigner));
            }

            template <class AtomIterator>
            insert_result_t insert(AtomIterator& begin, AtomIterator aend, value_type value)
            {
                return insert(begin, std::move(aend), [&]() -> const value_type&{
                    return value;
                    });
            }

            template <class AtomContainer>
            std::pair<iterator, bool> insert(const AtomContainer& container, value_type value)
            {
                auto b = std::begin(container);
                return insert(b, std::end(container), value);
            }

            /**
            *  Insert key-value below the prefix specified by parameter `of_prefix`. In fact on success you 
            *   add the key that looks like concatenation of `of_prefix.key() + atom_string_t(begin, aend)`
            *
            *   \param of_prefix - iterator pointing to the existing entry. If iterator obsolete (for example
            *       prefix has been erased), then this method create artificial key by concatenation of prefix and 
            *       [begin, end) and behaves like regular insert: 
            *       \code insert( of_prefix.key() + atom_string_t(begin, aend), value);
            *       \endcode
            *   \param begin - start iterator of key string 
            *   \param aend - end iterator of key string
            *   \tparam FPayloadFactory functor that assigns value. It must have signature `void (payload_t& )` 
            *   \return a pair of iterator and bool. Iterator pointing to the element matched to key 
            *       `of_prefix.key() + atom_string_t(begin, aend)`. Boolean value denoting if the insertion succeeded
            *       or result key is a dupplicate. For empty string `(begin == aend)` method immidiatly returns 
            *       pair `(end(), false)`
            */
            template <class AtomIterator, class FPayloadFactory>
            std::enable_if_t<std::is_invocable_v<FPayloadFactory>, insert_result_t>
                prefixed_insert(
                iterator& of_prefix, AtomIterator begin, AtomIterator aend, FPayloadFactory&& value_factory)
            {
                if (begin == aend)
                    return std::make_pair(end(), false); //empty string is not operatable

                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true/*commit automatically*/);
                OP::common::atom_string_t fallback_key;
                auto sync_res = sync_iterator(of_prefix, &fallback_key);
                if (!sync_res)
                { //no entry for previous iterator
                    return insert(fallback_key.append(begin, aend), std::move(value_factory));
                }
                auto result = std::make_pair(of_prefix, true);
                alter_navigation(result.first);
                result.second = !insert_impl(
                    result.first, begin, aend, std::move(value_factory));
                return result;
            }

            /** Same as `prefixed_insert(iterator&, AtomIterator, AtomIterator, FPayloadFactory)` but instead of 
            * pair of source key iterators uses string-like container.
            *
            *   \tparam TStringLike - string-like type to specify the key (like: atom_string_t, 
            *       std::string, std::string_view, std::u8string, ...)
            *   \tparam FPayloadFactory - functor with signature `value_t ()` or `const value_t& ()`
            */
            template <class TStringLike, class FPayloadFactory>
            std::enable_if_t<std::is_invocable_v<FPayloadFactory>, insert_result_t>
                prefixed_insert(iterator& of_prefix, const TStringLike& container, FPayloadFactory&& payload_factory)
            {
                return prefixed_insert(of_prefix, std::begin(container), std::end(container), std::move(payload_factory));
            }

            /** Same as `prefixed_insert(iterator&, AtomIterator, AtomIterator, FPayloadFactory)` but instead of 
            * value_factory uses explicit value assignment 
            *
            *   \param value - value to assign to new key
            */
            template <class AtomIterator>
            insert_result_t prefixed_insert(iterator& of_prefix, AtomIterator begin, AtomIterator aend, value_type value)
            {
                return prefixed_insert(
                    of_prefix, std::move(begin), std::move(aend), 
                    [&]() -> const value_type&{
                        return value;
                    });
            }

            /** Same as `prefixed_insert(iterator&, AtomIterator, AtomIterator, payload_t)` but instead of 
            * pair of source key iterators uses string-like container.
            *
            *   \tparam TStringLike - string-like type to specify the key (like: atom_string_t, 
            *       std::string, std::string_view, std::u8string, ...)
            */
            template <class TStringLike>
            insert_result_t prefixed_insert(iterator& of_prefix, const TStringLike& container, value_type payload)
            {
                return prefixed_insert(of_prefix, std::begin(container), std::end(container), payload);
            }

            /**
            *   @return number of items updated (1 or 0)
            */
            size_t update(iterator& pos, value_type value)
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true);
                
                if (!sync_iterator(pos) || pos.is_end())
                { //no entry for previous iterator
                    return 0;
                }

                return update_impl(pos, std::move(value));
            }

            /**
            * Update or insert value specified by key that formed as `[begin, end)`.
            * @param value - payload to be assigned anyway
            * @return pair of iterator and boolean indicator. When insert succeeded iterator is a position of
            *       just inserted item, otherwise it points to already existing key. Boolean indicator is false when
            *       item already exists and true when it was inserted
            */
            template <class AtomIterator>
            std::pair<iterator, bool> upsert(AtomIterator begin, AtomIterator aend, payload_t&& value)
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
            std::pair<iterator, bool> upsert(const AtomContainer& key, payload_t&& value)
            {
                return upsert(std::begin(key), std::end(key), std::move(value));
            }
            /**Update or insert value specified by key that formed as `prefix.key + [begin, end)`. 
            *
            * \param value - payload to be assigned or updated
            * \return a pair of iterator and bool. Iterator pointing to the element matched to key 
            *       `of_prefix.key() + atom_string_t(begin, aend)`. Boolean value denoting if the insertion succeeded
            *       or result key is a dupplicate. For empty string `(begin == aend)` method immidiatly returns 
            *       pair `(end(), false)`
            */
            template <class AtomIterator>
            insert_result_t prefixed_upsert(iterator& of_prefix, AtomIterator begin, AtomIterator aend, value_type value)
            {
                if (begin == aend)
                    return std::make_pair(end(), false); //empty string is not operatable
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true/*commit automatically*/);
                OP::common::atom_string_t fallback_key;
                
                if (!sync_iterator(of_prefix, &fallback_key))
                { //no entry for previous iterator, just insert
                    return insert(fallback_key.append(begin, aend), std::move(value));
                }
                auto value_assigner = [&]() ->const value_type &{
                    return value;
                };

                auto result = std::make_pair(of_prefix, true);
                alter_navigation(result.first);
                if (insert_impl(
                    result.first, begin, aend, value_assigner))
                {
                    result.second = false;//already exists
                    update_impl(result.first, std::move(value));
                }
                return result;
            }

            template <class AtomContainer>
            std::pair<iterator, bool> prefixed_upsert(iterator& prefix, const AtomContainer& container, payload_t value)
            {
                return prefixed_upsert(prefix, std::begin(container), std::end(container), std::move(value));
            }

            iterator erase(iterator& pos, size_t* count = nullptr)
            {
                if (count) { *count = 0; }

                OP::vtm::TransactionGuard op_g(_topology->segment_manager()
                    .begin_transaction(), true);

                if (!sync_iterator(pos) || pos.is_end())
                    return end();

                return erase_impl(pos, count);
            }

            /**Simplified form of erase(iterator&, size_t*)*/
            iterator erase(iterator&& pos)
            {
                iterator snapshot = std::move(pos);
                return erase(snapshot, nullptr);
            }

            /**
            * Erase element pointed by iterator `prefix` and every children entries below. 
            * \param prefix iterator pointing to the valid position, at exit it point to the next 
            *       lowest existing element or `end()` if no such one.
            * \return number of erased items
            */
            size_t prefixed_erase_all(iterator& prefix, bool erase_prefix = true)
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true);
                if (!sync_iterator(prefix) || prefix.is_end())
                { 
                    return 0;
                }

                auto rat = prefix.rat();//not a ref!
                if (erase_prefix && is_not_set(rat.terminality(), Terminality::term_has_child))
                { //no child below, so only 1 to erase
                    size_t counter = 0;
                    prefix = erase_impl(prefix, &counter);
                    return counter;
                }
                // here iterator definitely has a child
                auto parent_wr_node = accessor<node_t>(*_topology, rat.address());

                std::stack<FarAddress> to_process;
                to_process.push(parent_wr_node->get_child(
                    *_topology, static_cast<atom_t>(rat.key())));
                parent_wr_node->remove_child(*_topology, static_cast<atom_t>(rat.key()));
                size_t erased_terminals = 0; //includes one referencing 
                while (!to_process.empty())
                {
                    auto node_addr = to_process.top();
                    to_process.pop();

                    auto wr_node = accessor<node_t>(*_topology, node_addr);
                    erased_terminals += wr_node->erase_all(*_topology, to_process);
                    remove_node(wr_node);
                }
                const auto& new_back = prefix.rat(
                    terminality_and(~Terminality::term_has_child));//avoid way-down
                //need additional variable since #erase will decrement counter as well
                auto effective_decrease_number = -static_cast<std::make_signed_t<size_t>>(erased_terminals);
                if (erase_prefix && all_set(new_back.terminality(), Terminality::term_has_data))
                {
                    size_t counter = 0;
                    prefix = erase_impl(prefix, &counter);
                    assert(counter);//otherwise terminality flag must be altered
                    erased_terminals += counter;
                }

                _topology->template slot<TrieResidence>()
                    .update([&](auto& header){
                        header._count += effective_decrease_number; //number of terminals
                        header._version = ++this->_version; // version of trie
                        prefix._version = this->_version;
                    });

                if (!prefix.is_end())
                {
                    prefix.rat(
                        node_version(parent_wr_node->_version),
                        terminality_and(~Terminality::term_has_child));
                }
                return erased_terminals;
            }

            /**
            *   Remove all that starts with string specified by `prefix`. The fundamental
            * difference with #prefixed_erase that this method removes any entries
            * satisfied prefix condition.
            */
            template <class StringLike>
            size_t prefixed_key_erase_all(const StringLike& prefix)
            {
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true);
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
                            _next(it);
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //autocommit on return
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
                OP::vtm::TransactionGuard op_g(_topology->segment_manager().begin_transaction(), true); //autocommit on return
                return f(*this);
            }

        private:

            typedef FixedSizeMemoryManager<node_t, initial_node_count> node_manager_t;
            using topology_t = SegmentTopology<
                TrieResidence,
                node_manager_t,
                HeapManagerSlot/*Memory manager must go last*/>;
            std::unique_ptr<topology_t> _topology;

            /** This variable is a global version indicator, since 
            * this is a trie-global resource it cannot rely on 
            * transaction mechanism. For example what if 2 parallel
            * transactions increase the version, after one transaction 
            * rollbacks its. As a result all iterators may "think" referencing
            * correct version, but in fact they use "rolled back" value.
            * To avoid this _version is trie-global and always increasing
            * (never rolled back)
            */
            std::atomic<std::uint64_t> _version = 0;
            
            /** Cached result of root node to avoid often referencing to 
            * persisted TrieResidence::TrieHeader::_root
            */ 
            FarAddress _root = {};

        private:
            Trie(std::shared_ptr<TSegmentManager>& segments) noexcept
                : _topology{ std::make_unique<topology_t>(segments) }
            {

            }
            /** Create new node with default requirements.
            * It is assumed that exists outer transaction scope.
            * \param level - the hint what level of trie this node belongs. 0 - is 
            *   for root node, for most cases default (1) is a good hint how many 
            *   storage entries to allocate
            */
            FarAddress new_node(size_t level = 1)
            {
                TrieOptions options; //@! temp - just default impl. Need add heuristic to allocate mem according to level
                auto node_addr = _topology->template slot<node_manager_t> ()
                    .allocate(options.init_node_size(level));

                auto wr_node = accessor<node_t>(*_topology, node_addr);
                wr_node->create_interior(*_topology);
                _topology->template slot<TrieResidence>()
                    .update([this](auto& header) {
                        ++header._nodes_allocated;
                    });
                return node_addr;
            }

            void remove_node(WritableAccess<node_t>& wr_node)
            {
                wr_node->destroy_interior(*_topology);
                _topology->template slot<node_manager_t>().deallocate(
                    wr_node.address());
                _topology->template slot<TrieResidence>()
                    .update([this](auto& header) {
                        --header._nodes_allocated;
                    });
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
                wr_node.move_to(*_topology, key, in_stem_pos, new_node_addr);
                //wr_node.set_child(*_topology, key, new_node_addr);
                break_position.rat(terminality_or(Terminality::term_has_child));

                return new_node_addr;
            }
            /**Place string to node without any additional checks
            * \tparam FPayloadFactory functor that has the signature `void (payload_t&)`
            * \return updated version of trie (matched to current transaction)
            */
            template <class FPayloadFactory>
            std::uint64_t unconditional_insert(iterator& result, FPayloadFactory fassign)
            {
                const auto& back = result.rat();
                assert(back.key() < 256);
                atom_t key = static_cast<atom_t>(back.key());
                auto wr_node = accessor<node_t>(*_topology, back.address());
                assert(back.stem_size() != dim_nil_c );
                wr_node->insert(
                    *_topology, key, 
                    result._prefix.end() - back.stem_size(), result._prefix.end(),
                    [&](auto& raw_payload) {
                        storage_converter_t::serialize(*_topology,
                            fassign(),
                            raw_payload
                        );
                    });
                result.rat(
                    node_version(wr_node->_version),
                    terminality_or(Terminality::term_has_data)
                );
                // condition `begin == end` is never happens
                std::uint64_t version = ++this->_version; // version of trie
                _topology->template slot<TrieResidence>()
                    .update([&](auto& header){
                        ++header._count; //number of terminals
                        header._version = version;
                    });
                return version;
            }

            template <class AtomIterator>
            StemCompareResult mismatch(iterator& iter, AtomIterator& begin, AtomIterator end) const
            {
                using node_data_t = typename node_t::NodeData;
                StringMemoryManager string_memory_manager(*_topology);

                StemCompareResult mismatch_result = StemCompareResult::equals;
                for (FarAddress node_addr = iter.rat().address(); 
                    begin != end 
                    && !node_addr.is_nil()
                    && OP::utils::any_of<StemCompareResult::equals, StemCompareResult::stem_end>(
                        mismatch_result);)
                {
                    auto node =
                        view<node_t>(*_topology, node_addr);
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
                    mismatch_result = node->rawc(*_topology, step_key,
                        [&](const node_data_t& node_data) -> StemCompareResult {
                            node_addr = node_data._child;//if exists discover next child node
                            StemCompareResult stem_matches = StemCompareResult::equals;
                            if (!node_data._stem.is_nil())
                            { //stem exists, append to out iterator
                                
                                auto result_stem_size = static_cast<dim_t>(
                                    string_memory_manager.get(node_data._stem, [&](atom_t c) -> bool {
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
                    if (mismatch_result == StemCompareResult::stem_end && has_child)
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
            * something - then `this->mismatch` should return `equals`
            */
            template <class FValueEval>
            void insert_mismatch_string_end(
                WritableAccess<node_t>& wr_node,
                iterator& iter, FValueEval&& f_value_eval)
            {
                //assert(!wr_node->has_value(step_key));
                auto back = iter.rat();//no ref, copy!
                assert(back.key() <= std::numeric_limits<atom_t>::max());
                atom_t step_key = static_cast<atom_t>(back.key());

                wr_node->raw(*_topology, step_key, [&](auto& src_entry){
                    if (!src_entry._stem.is_nil())
                    {
                        auto new_node_addr = new_node(iter.node_count() + 1);
                        auto target_node = accessor<node_t>(*_topology, new_node_addr);
                        wr_node->move_from_entry(*_topology, step_key, src_entry, back.stem_size(), target_node);
                    }
                    assert(!wr_node->has_value(step_key));
                    //assign (new!) value to the just freed position
                    payload_manager_t::allocate(*_topology, src_entry._value);

                    wr_node->set_raw_factory_value(
                        *_topology, step_key, src_entry, 
                        [&](payload_t& dest) {
                            
                            storage_converter_t::serialize(*_topology, f_value_eval(), dest);
                        });
                });
                iter.rat(
                    terminality_or(
                        Terminality::term_has_child | Terminality::term_has_data),
                    node_version(wr_node->_version)
                );

                _topology->template slot<TrieResidence>()
                    .update([new_ver = ++this->_version](auto& header){
                        ++header._count; //number of terminals
                        header._version = new_ver; // version of trie
                    });
            }
            
            size_t update_impl(iterator& pos, value_type value)
            {
                const auto& back = pos.rat();
                assert(all_set(back.terminality(), Terminality::term_has_data));

                auto wr_node = accessor<node_t>(*_topology, back.address());
                atom_t up_key = static_cast<atom_t>(back.key());
                wr_node->raw(*_topology, up_key, [&](auto& node_data) {
                        wr_node->set_raw_factory_value(*_topology, up_key, node_data, [&](auto& dest) {
                            storage_converter_t::reassign(*_topology, value, dest);
                        });
                });

                pos.rat(node_version(wr_node->_version));
                _topology->template slot<TrieResidence>()
                    .update([&](auto& header){
                        header._version = this->_version;
                        pos._version = header._version;// version of trie
                    });
                return 1;
            }

            /**Insert or update value associated with key specified by pair [begin, end). 
            * The value is passed as 2 separate functors evaluated on demand - one for 
            * insert other for update result.
            * \param iter - iterator to start, note it MUST be already synced. At exit
            *               it contains valid and actual points to just updated/inserted
            *
            * \tparam FValueFactory functor that has signature `void (payload_t&)`
            *
            * \return true if value already exists, false if new value has been added
            */
            template <class AtomIterator, class FValueFactory>
            bool insert_impl(
                iterator& iter, AtomIterator begin, AtomIterator end, FValueFactory&& value_factory)
            {
                if (iter.is_end())
                { //start from root node
                    iter.push(
                        address(_root)
                    );
                }
                
                StemCompareResult mismatch_result = mismatch(iter, begin, end);
                
                if (begin == end && mismatch_result == StemCompareResult::equals)//full match
                {
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
                    auto wr_node = accessor<node_t>(*_topology, back.address());

                    if (mismatch_result == StemCompareResult::string_end)
                    {
                        insert_mismatch_string_end(wr_node, iter, std::move(value_factory));
                        return false;
                    }

                    auto new_node_addr = new_node(iter.node_count()+1);
                    if (mismatch_result == StemCompareResult::stem_end)
                    {
                        assert(is_not_set(back._terminality, Terminality::term_has_child));
                        wr_node->set_child(
                            *_topology, step_key, new_node_addr);
                        iter.rat(node_version(wr_node->_version));
                        iter.push(
                            key(*begin++),
                            address(new_node_addr)
                        );
                        iter.update_stem(begin, end);
                    }
                    else //stem is fully processed
                    {
                        auto target_node = accessor<node_t>(*_topology, new_node_addr);

                        wr_node->move_to(*_topology, step_key, back.stem_size(), target_node);
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
                iter._version = unconditional_insert(iter, std::move(value_factory));
                return false;//brand new entry
            }
            
            iterator erase_impl(iterator& pos, size_t* count = nullptr)
            {
                auto result{ pos };
                _next(result);
                bool erase_child_and_exit = false; //flag mean stop iteration

                for (bool first = true; pos.node_count(); pos.pop(), first = false)
                {
                    const auto& back = pos.rat();
                    auto wr_node = accessor<node_t>(*_topology, back.address());
                    if (erase_child_and_exit)
                    {//previous node may leave reference to child
                        wr_node->remove_child(*_topology, static_cast<atom_t>(back.key()));
                        pos.rat(
                            terminality_and(~Terminality::term_has_child),
                            node_version(wr_node->_version)
                        );
                    }

                    if (!wr_node->erase(*_topology, static_cast<atom_t>(back.key()), first))
                    { //don't continue if exists a child node
                        pos.rat(
                            terminality_and(~Terminality::term_has_data),
                            node_version(wr_node->_version));
                        break;
                    }
                    //remove entire node if it is not a root
                    if (back.address() != this->_root)
                    {
                        remove_node(wr_node);
                        erase_child_and_exit = true;
                    }
                }
                const std::uint64_t new_ver = ++this->_version;
                _topology->template slot<TrieResidence>()
                    .update([&](auto& header) {
                    --header._count; //number of terminals
                    header._version = new_ver; // version of trie
                        });
                if (count) { ++*count; }
                return result;
            }

            /** Prepares `result_iter` to further navigation deep, if it is empty 
            * method prepares navigation from root node
            */
            bool navigation_mode(iterator& result_iter) const
            {
                FarAddress next_address;
                if (result_iter.is_end())
                { //start from root node
                    next_address = _root;
                }
                else
                {
                    if (!all_set(result_iter.rat().terminality(), Terminality::term_has_child))
                        return false;//no way down
                    const auto& back = result_iter.rat();
                    next_address = view<node_t>(
                        *_topology, back.address())
                        ->get_child(*_topology, static_cast<atom_t>(back.key()))
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
                    accessor<node_t>(*_topology, back.address())
                        ->set_child(*_topology, static_cast<atom_t>(back.key()), addr);
                    result_iter.push(
                        address(addr)
                    );
                }
            }
            /**Return not fully valid iterator that matches common part specified by [begin, end)
            * \return reason why iterator unmatches to query string
            */
            template <class Atom>
            StemCompareResult common_prefix(Atom& begin, Atom end, iterator& result_iter) const
            {
                StemCompareResult retval = StemCompareResult::unequals;
                if (begin == end || !navigation_mode(result_iter))
                { //nothing to consider
                    return retval;
                }

                retval =
                    mismatch(result_iter, begin, end);
                return retval;
            }
            

            template <class FChildLocator>
            iterator children_navigation(iterator& of_this, FChildLocator locator) const
            {
                if (!sync_iterator(of_this) || of_this == end() ||
                    is_not_set(of_this.rat().terminality(), Terminality::term_has_child))
                {
                    return end(); //no way down
                }
                iterator result(of_this);
                const auto& back = result.rat();
                assert(back.key() < 256);
                FarAddress child = back.address();
                auto ro_node = view<node_t>(*_topology, child);
                child = ro_node->get_child(*_topology, 
                    static_cast<atom_t>(back.key()));
                enter_deep_until_terminal(child, result, locator);
                return result;
            }
            
            /**
            * @return true when result matches exactly to specified string [begin, aend)
            */
            template <class Atom>
            bool lower_bound_impl(Atom& begin, Atom aend, iterator& prefix) const
            {
                if (begin == aend)
                {
                    prefix = end();
                    return false;
                }
                if (!navigation_mode(prefix))
                {
                    //prefix.pop();
                    _next(prefix);
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
                        _next(prefix);
                    }
                    return false; //not an exact match
                }
                case StemCompareResult::no_entry:
                {
                    auto back = prefix.rat(stem_size(0));//not a reference
                    auto [ok, child] = load_iterator(
                        back.address(), prefix,
                        [&](ReadonlyAccess<node_t>& ro_node) 
                        { //on absence of entry just try find bigger in the same node
                            return ro_node->next_or_this(static_cast<atom_t>(back.key()));
                        },
                        &iterator::update_back);

                    if (!ok)
                    {
                        prefix.pop();
                        _next(prefix, false/*no reason to enter deep*/);
                        return false; //no an exact match
                    }

                    if (is_not_set(prefix.rat().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(back.address(), prefix,
                            [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); });
                    }
                    return false; //not an exact match
                }
                case StemCompareResult::string_end:
                {
                    auto [ok, child] = load_iterator(
                        prefix.rat().address(), prefix,
                        [&](ReadonlyAccess<node_t>& ro_node) {
                            return NullableAtom{ prefix.rat().key() };
                        },
                        &iterator::update_back);
                    assert(ok); //empty node is impossible there
                    if (is_not_set(prefix.rat().terminality(), Terminality::term_has_data))
                    {
                        enter_deep_until_terminal(child, prefix,
                            [](ReadonlyAccess<node_t>& ro_node) { 
                                return ro_node->first(); 
                            });
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
            * \return bool where true means success sync (or no sync was needed) and second string has no sense,
            *         while false means inability to sync (entry was erased);
            * \param fallback atomic_string_t that set overall result false origin key of iterator, because on
            *       unsuccessful sync at exit iterator is damaged. Pointer may be omitted if you don't need recovery
            */
            bool sync_iterator(iterator& it, OP::common::atom_string_t* fallback = nullptr) const
            {
                const std::uint64_t this_ver = this->version();

                if (this_ver == it.version()) //no sync is needed
                    return true;
                it._version = this_ver;
                dim_t order = 0;
                size_t prefix_length = 0;
                //take each node of iterator and check against current version of real node
                for (const auto& i : it._position_stack)
                {
                    auto node = view<node_t>(*_topology, i.address());
                    if (node->_hash_table.is_nil())
                    {//may be iterator so old, so node have been removed
                        it = end();
                        return false;
                    }
                    if (node->_version != i.version())
                    {
                        if (fallback)
                        {
                            *fallback = it.key(); //need make copy since next instructions corrupt the iterator
                        }
                        auto repeat_search_key = it.key().substr(prefix_length); //cut prefix str
                        it._prefix.resize(prefix_length);
                        it._position_stack.erase( //cut stack
                            it._position_stack.begin() + order, 
                            it._position_stack.end()); 
                        auto suffix_begin = repeat_search_key.begin();
                        auto suffix_end = repeat_search_key.end();
                        if (!lower_bound_impl(suffix_begin, suffix_end, it))
                        { 
                            return false;
                        }
                        return true;
                    }
                    assert(i.stem_size() != dim_nil_c);
                    prefix_length += i.stem_size() + 1;
                    ++order;
                }
                return true;
            }

            /**
            * \tparam FFindEntry - function `NullableAtom (ReadonlyAccess<node_t>&)` 
            *           that resolve index inside node;
            * \tparam FIteratorUpdate - pointer to one of iterator members - either to 
            *           update 'back' position or insert new one to iterator;
            * \return pair (bool, FarAddress) where bool indicate if further navigation 
            *           is possible, and FarAddress where to make next in-deep step.
            */
            template <class FFindEntry, class FIteratorUpdate>
            std::pair<bool, FarAddress> load_iterator(
                const FarAddress& node_addr, iterator& dest, 
                FFindEntry pos_locator, FIteratorUpdate iterator_update) const
            {
                auto ro_node = view<node_t>(*_topology, node_addr);
                NullableAtom pos = pos_locator(ro_node);
                if (!pos)
                { //no first
                    return std::make_pair(false, FarAddress());
                }

                position_t root_pos(
                    address(node_addr),
                    key(pos.value()),
                    node_version(ro_node->_version)
                );
                return ro_node->rawc(*_topology, pos.value(),
                    [&](const auto& node_data) {
                        if (!node_data._stem.is_nil())
                        {//if stem exists should be placed to iterator
                            StringMemoryManager smm(*_topology);
                            OP::common::atom_string_t buffer;
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
                                (ro_node->has_value(pos.value()) ? Terminality::term_has_data : Terminality::term_no)
                                | (ro_node->has_child(pos.value()) ? Terminality::term_has_child : Terminality::term_no)
                            )
                        );
                        return std::make_pair(true, node_data._child);
                    });

            }

            void _next(iterator& i, bool way_down = true) const
            {
                while (!i.is_end())
                {
                    const auto& back = i.rat();
                    //try enter deep
                    if (way_down && all_set(back.terminality(), Terminality::term_has_child))
                    {   //get address of child
                        auto ro_node = view<node_t>(*_topology, back.address());
                        auto child_addr = ro_node->get_child(
                            *_topology, static_cast<atom_t>(back.key()));
                        enter_deep_until_terminal(child_addr, i, 
                            [](ReadonlyAccess<node_t>& ro_node) { 
                                return ro_node->first(); 
                            });
                        return;
                    }
                    //try navigate right from current position
                    auto [ok, child] = load_iterator(i.rat().address(), i,
                        [&i](ReadonlyAccess<node_t>& ro_node)
                        {
                            //don't optimize `i.rat` since i may change
                            return ro_node->next((atom_t)i.rat().key());
                        },
                        &iterator::update_back);
                    if (ok)
                    { //navigation right succeeded
                        if (all_set(i.rat().terminality(), Terminality::term_has_data))
                        {//i already points to correct entry
                            return;
                        }
                        enter_deep_until_terminal(child, i,
                            [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); });
                        return;
                    }
                    //here since no way neither down nor right
                    way_down = false;
                    i.pop();
                }
            }

            /** Enters deep to child hierarchy until first terminal entry found
            \tparam FChildLocator - lambda to locate first meaningful position inside child
                signature must match `void (ReadonlyAccess<node_t>& )`
                for example to seek first child-or-value entry use: \code
                [](ReadonlyAccess<node_t>& ro_node) { return ro_node->first(); }
                \endcode
            */
            template <class FChildLocator>
            void enter_deep_until_terminal(FarAddress start_from, iterator& i, FChildLocator locator) const
            {
                bool ok = true;
                do
                {
                    assert(!start_from.is_nil());
                    std::tie(ok, start_from) = load_iterator(start_from, i,
                        locator,
                        &iterator::emplace);
                    assert(ok); //empty nodes are not allowed
                } while (is_not_set(i.rat().terminality(), Terminality::term_has_data));
            }
        };

    } //ns:trie
}//ns:OP

#endif //_OP_TRIE_TRIE__H_
