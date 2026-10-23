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
#include <op/common/ConditionalIterator.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>

#include <op/trie/TrieNode.h>
#include <op/trie/TrieIterator.h>
#include <op/trie/TrieResidence.h>
#include <op/trie/StoreConverter.h>
#include <op/trie/MixedAdapter.h>
#include <op/trie/TrieOptions.h>
#include <op/trie/StringMemoryManager.h>

namespace OP::trie
{
    enum class TrieCDC
    {
        /** Number of nodes has been increased.
        * Arguments: 
        * const Trie::key_t& - prefix string of new node.
        */
        node_created = 1,
        /** Number of nodes going to decrease 
        * Arguments:
        * const Trie::key_t& - prefix string of deleting node.
        */
        before_node_delete
    };


    template <
        class TKeyString,
        class TPayloadManager
    >
    class Trie : public std::enable_shared_from_this<Trie<TKeyString, TPayloadManager>>
    {
    public:
        using atom_t = OP::common::atom_t;
        using dim_t = OP::vtm::dim_t;
        using FarAddress = OP::vtm::FarAddress;
        using NullableAtom = vtm::NullableAtom;
        using trie_t = Trie<TKeyString, TPayloadManager>;
        using payload_manager_t = TPayloadManager;
        using payload_t = typename payload_manager_t::payload_t;
        using this_t = trie_t;
        using iterator = TrieIterator<this_t>;
        using value_type = typename payload_manager_t::source_payload_t;
        using node_t = TrieNode<payload_manager_t>;
        using position_t = TriePosition;
        using key_t = TKeyString;
        using key_view_t = std::basic_string_view<typename key_t::value_type>;
        using insert_result_t = std::pair<iterator, bool>;
        using storage_converter_t = typename payload_manager_t::storage_converter_t;

        using event_supplier_t = OP::events::EventSupplier<
            Assoc<TrieCDC::node_created, const key_t&>,
            Assoc<TrieCDC::before_node_delete, const key_t&>
        >;

        virtual ~Trie() = default;

        static std::shared_ptr<Trie> create_new(std::shared_ptr<TrieOptions> options)
        {
            assert(options->residence_service().root().is_nil());//no previous allocation
            
            //create new file
            auto new_trie = std::shared_ptr<this_t>(new this_t(options));
            //make root for trie
            OP::vtm::TransactionGuard op_g(options->transaction_service().begin_transaction(), true); //invoke begin/end write-op
            std::tie(new_trie->_root, std::ignore) = new_trie->new_node(0);
            new_trie->_options->residence_service().set_root(new_trie->_root);
            return new_trie;
        }

        static std::shared_ptr<Trie> open(std::shared_ptr<TrieOptions> options)
        {
            auto existing_trie = std::shared_ptr<this_t>(new this_t(std::move(options)));

            existing_trie->_version = existing_trie->_options->residence_service().version();
            existing_trie->_root = existing_trie->_options->residence_service().root();
            return existing_trie;
        }

        /**Total number of items*/
        std::uint64_t size() const
        {
            return _options->residence_service().count();
        }

        trie_version_t version() const
        {
            return _options->residence_service().version();
        }

        /**Number of allocated nodes*/
        std::uint64_t nodes_count()
        {
            return _options->residence_service().nodes_allocated();
        }

        iterator begin() const
        {
            OP::vtm::TransactionGuard op_g(_options->transaction_service().begin_transaction(), false); //place all RO operations to atomic scope
            auto next_addr = _root;
            iterator i(this);
            bool ok = true;
            auto locator = [](const node_t& ro_node) { return ro_node.first(); };
            do {
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
            OP::vtm::TransactionGuard op_g(_options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope
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
            OP::vtm::TransactionGuard op_g(_options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope

            if (!sync_iterator(i))
                return;
            while (!i.is_end())
            {
                auto [ok, child] = load_iterator(
                    i.rat().address(), i,
                    [&i](const node_t& ro_node)
                    {
                        //don't optimize i.rat() since iterator updated inside `load_iterator`
                        return ro_node.next((atom_t)i.rat_key());
                    },
                    &iterator::update_back);
                if (ok)
                { //navigation right succeeded
                    if (all_set(i.rat().terminality(), Terminality::term_has_data))
                    {//i already points to the correct entry
                        return;
                    }
                    enter_deep_until_terminal(child, i,
                        [](const node_t& ro_node) { return ro_node.first(); });
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
            OP::vtm::TransactionGuard op_g(_options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope

            if (!sync_iterator(i) || key.empty())
                return;
            auto [mis_it, mis_key] = std::mismatch(
                i.key().begin(), i.key().end(), key.begin(), key.end());
            if (mis_key == key.end())
            {
                _next(i, true); //no matches with key
            }
            else
            {
                while (mis_it < i.key().end())
                    i.pop();
                lower_bound_impl(mis_key, key.end(), i);
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
            OP::vtm::TransactionGuard op_g(_options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope
            iterator i(this);
            auto nav = common_prefix(begin, aend, i);
            if (begin != aend) //no such prefix since begin wasn't exhausted
                return end();
            auto i_beg = i;
            //find next position that doesn't matches to prefix
            //nothing to do for: if (nav.compare_result == StemCompareResult::equals //prefix fully matches to existing terminal
            if (nav == StemCompareResult::string_end) //key partially matches to some prefix
            { //correct string at the back of iterator
                auto [ok, child] = load_iterator(i_beg.rat().address(), i_beg,
                    [&i](const node_t&) {
                        return NullableAtom{ i.rat_key() };
                    },
                    &iterator::update_back);
                assert(ok);//tail must exists
                if (is_not_set(i_beg.rat().terminality(), Terminality::term_has_data))
                {
                    enter_deep_until_terminal(child, i_beg,
                        [](const node_t& ro_node) { return ro_node.first(); });
                }
            }
            else if (nav != StemCompareResult::equals)
                i_beg = end();

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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), false); //place all RO operations to atomic scope
            key_t lookup_key;
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), false); //place all RO operations to atomic scope
            iterator it(this);
            if (lower_bound_impl(begin, aend, it))
            {
                return begin == aend ? it : end();// StemCompareResult::stem_end or StemCompareResult::string_end
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), false); //place all RO operations to atomic scope
            auto b = std::begin(container);
            iterator iter = end();

            StemCompareResult nav_res = common_prefix(b, std::end(container), iter);

            return (nav_res == StemCompareResult::equals
                && (iter.rat().terminality() & Terminality::term_has_data));
        }

        template <class AtomString>
        bool check_prefix_exists(const AtomString& container) const
        {
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), false); //place all RO operations to atomic scope
            auto b = std::begin(container);
            iterator iter = end();

            StemCompareResult nav_res = common_prefix(b, std::end(container), iter);

            return nav_res == StemCompareResult::equals || nav_res == StemCompareResult::string_end;
        }

        /** \return check if iterator points to the prefix that has some child.
        *   Method is always false for `end()` or invalid iterators.
        */
        bool has_child(iterator prefix) const
        {
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), false); //place all RO operations to atomic scope
            if (sync_iterator(prefix) && !prefix.is_end())
            {
                const auto& back = prefix.rat();
                return all_set(back.terminality(), Terminality::term_has_child);
            }
            return false;
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope
            return children_navigation(of_this,
                [](const node_t& ro_node) { return ro_node.first(); });
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true); //place all RO operations to atomic scope
            return children_navigation(of_this,
                [](const node_t& ro_node) { return ro_node.last(); });
        }
        /**
        *   @return sequence-factory that embrace all records by pair `[ begin(), end() )` but in more effecient way.
        */
        auto range() const
        {
            return make_mixed_sequence_factory(this->shared_from_this());
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
            key_t prefix(begin, aend);
            return prefixed_range(prefix);
        }

        /**
        *   Construct a range that contains all strings started with specified prefix
        * @param prefix is any string of bytes that supports std::begin / std::end iteration
        */
        template <class AtomContainer>
        auto prefixed_range(const AtomContainer& prefix) const
        {
            return
                make_mixed_sequence_factory(
                    std::const_pointer_cast<const this_t>(this->shared_from_this()),
                    Ingredient::PrefixedBegin(prefix),
                    Ingredient::PrefixedLowerBound(prefix),
                    Ingredient::PrefixedInRange(StartWithPredicate(prefix))
                );
        }

        /**Return range that allows iterate all immediate children of specified prefix*/
        auto children_range(const iterator& of_this) const
        {
            return make_mixed_sequence_factory(
                std::const_pointer_cast<const this_t>(this->shared_from_this()),
                Ingredient::ChildBegin{ of_this },
                Ingredient::ChildInRange{ StartWithPredicate(of_this.key()) },
                Ingredient::SiblingNext{}
            );
        }

        /**Return range that allows iterate all immediate children of specified prefix
        * \tparam AtomContainer - any string-like character enumeration
        */
        template <class AtomContainer>
        auto children_range(const AtomContainer& of_key) const
        {
            return make_mixed_sequence_factory(
                std::const_pointer_cast<const this_t>(this->shared_from_this()),
                Ingredient::ChildOfKeyBegin<AtomContainer>{ of_key },
                Ingredient::ChildInRange{ StartWithPredicate<AtomContainer>(of_key) },
                Ingredient::SiblingNext{}
            );
        }

        /**Return range that allows iterate all siblings of specified prefix*/
        template <class AtomContainer>
        auto sibling_range(const AtomContainer& key) const
        {
            return make_mixed_sequence_factory(
                std::const_pointer_cast<const this_t>(this->shared_from_this()),
                Ingredient::Find<AtomContainer>(key),
                Ingredient::SiblingNext{}
            );

        }

        /** Utilize feature of Trie where all entries below the single prefix are lexicographically ordered.
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

        auto value_of(const iterator& pos) const
        {
            
            const auto& back = pos.rat();
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true);
            auto node =
                _options->memory_service().view<node_t>(back.address());

            if (all_set(back.terminality(), Terminality::term_has_data))
            {
                return node->get_value(*_options, pos.rat_key(), [this](const auto& ref) {
                    return OP::trie::store_converter::Storage<
                        typename payload_manager_t::source_payload_t
                    >::deserialize(*_options, ref);
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

            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction());

            auto result = std::make_pair(end(), true);
            result.second = insert_impl(
                result.first, begin, aend, std::forward<FPayloadFactory>(value_assigner));
            op_g.commit();

            return result;
        }

        template <class StringLike, class FPayloadFactory>
        std::enable_if_t<std::is_invocable_v<FPayloadFactory>, insert_result_t>
            insert(const StringLike& str, FPayloadFactory&& value_assigner)
        {
            auto b = str.begin();
            return insert(b, str.end(), std::forward<FPayloadFactory>(value_assigner));
        }

        template <class AtomIterator>
        insert_result_t insert(AtomIterator& begin, AtomIterator aend, value_type value)
        {
            return insert(begin, std::move(aend), [&]() -> const value_type& {
                return value;
                });
        }

        template <class AtomContainer>
        std::pair<iterator, bool> insert(const AtomContainer& container, value_type value)
        {
            auto b = std::begin(container);
            return insert(b, std::end(container), std::move(value));
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
        *       or result key is a duplicate. For empty string `(begin == aend)` method immediately returns
        *       pair `(end(), false)`
        */
        template <class AtomIterator, class FPayloadFactory>
        std::enable_if_t<std::is_invocable_v<FPayloadFactory>, insert_result_t>
            prefixed_insert(
                iterator& of_prefix, AtomIterator begin, AtomIterator aend, FPayloadFactory&& value_factory)
        {
            if (begin == aend)
                return std::make_pair(end(), false); //empty string is not operatable

            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true/*commit automatically*/);
            key_t fallback_key;
            auto sync_res = sync_iterator(of_prefix, &fallback_key);
            if (!sync_res)
            { //no entry of previous iterator
                return insert(fallback_key.append(begin, aend), std::move(value_factory));
            }
            auto result = std::make_pair(of_prefix, true);

            result.second = insert_impl(
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
            return prefixed_insert(
                of_prefix, std::begin(container), std::end(container), std::move(payload_factory));
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
                [&]() -> const value_type& {
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
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true);

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
        *       or result key is a duplicate. For empty string `(begin == aend)` method immediately returns
        *       pair `(end(), false)`
        */
        template <class AtomIterator>
        insert_result_t prefixed_upsert(iterator& of_prefix, AtomIterator begin, AtomIterator aend, value_type value)
        {
            if (begin == aend)
                return std::make_pair(end(), false); //empty string is not operatable
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true/*commit automatically*/);
            key_t fallback_key;

            if (!sync_iterator(of_prefix, &fallback_key))
            { //no entry for previous iterator, just insert
                return insert(fallback_key.append(begin, aend), std::move(value));
            }
            auto value_assigner = [&]() ->const value_type& {
                return value;
                };

            auto result = std::make_pair(of_prefix, false);
            result.second = insert_impl(result.first, begin, aend, value_assigner);

            if (!result.second)
            {//already exists
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

            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true);

            if (!sync_iterator(pos) || pos.is_end())
                return end();

            return erase_impl(pos, count);
        }

        /**Simplified form of erase(iterator&, size_t*)*/
        iterator erase(iterator&& pos, size_t* count = nullptr)
        {
            iterator snapshot = std::move(pos);
            return erase(snapshot, count);
        }

        /**
        * Erase element pointed by iterator `prefix` and every children entries below.
        * \param prefix iterator pointing to the valid position, at exit it points to the next
        *       lowest existing element or `end()` if no such one.
        * \return number of erased items
        */
        size_t prefixed_erase_all(iterator& prefix, bool erase_prefix = true)
        {
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true);
            if (!sync_iterator(prefix) || prefix.is_end() ||
                !(prefix.rat().terminality() & Terminality::term_has_data) //there is prefix entry, but for children (no value)
                )
            {
                return 0;
            }
            return prefixed_erase_all_impl(prefix, erase_prefix);
        }

        /**
        *   Remove all that starts with string specified by `prefix`. The fundamental
        * difference with #prefixed_erase that this method removes as well entries
        * that are matched to the prefix condition.
        */
        template <class StringLike>
        size_t prefixed_key_erase_all(const StringLike& prefix)
        {
            OP::vtm::TransactionGuard op_g(
                _options->transaction_service().begin_transaction(), true);
            iterator it;
            auto rest_prefix = std::begin(prefix);
            switch (mismatch(it, rest_prefix, std::end(prefix)))
            {
            case StemCompareResult::equals:
                [[fallthrough]];
            case StemCompareResult::string_end:
                return prefixed_erase_all_impl(it);
            default:
                break;
            }
            return 0;
        }

        ///**
        //*   Allows to apply multiple modification operations in a transaction. In fact this just decoration
        //* for \code
        //*  OP::vtm::TransactionGuard op_g(segment_manager().begin_transaction(), true);
        //*  try{
        //*  .. multiple operations that modifies trie ...
        //*  }catch(...){
        //*       op_g.rollback();
        //*       throw;
        //*  }
        //* \endcode
        //*/
        //template <class F>
        //auto apply(F& f) -> decltype(f(*this))
        //{
        //    OP::vtm::TransactionGuard op_g(_options->memory_service().begin_transaction(), true); //autocommit on return
        //    try {
        //        return f(*this);
        //    }
        //    catch (...) {
        //        op_g.rollback();
        //        throw;
        //    }
        //}

        ///**
        //*   Allows to apply multiple readonly operations in a transaction. In fact this just decoration
        //* for \code
        //*  OP::vtm::TransactionGuard op_g(segment_manager().begin_transaction(), false);
        //*  .. multiple operations that modifies trie ...
        //*  op_g.commit()
        //* \endcode
        //*/
        //template <class F>
        //auto apply(F const& f) const -> decltype(f(*this))
        //{
        //    OP::vtm::TransactionGuard op_g(_options->memory_service().begin_transaction(), true); //autocommit on return
        //    return f(*this);
        //}

    private:

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

        std::shared_ptr<TrieOptions> _options;


    private:
        Trie(std::shared_ptr<TrieOptions> options) noexcept
            : _options(std::move(options))
        {

        }
        /** Create new node with default requirements.
        * It is assumed that exists outer transaction scope.
        * \param level - the hint what level of trie this node belongs. 0 - is
        *   for root node
        */
        std::pair<FarAddress, node_t*> new_node(size_t level)
        {
            vtm::dim_t capacity = _options->init_node_size().init_node_size(level);
            auto node_addr = _options->allocator_service().allocate_for<node_t>();
            auto* node = _options->memory_service().access<node_t>(node_addr).make_new(capacity);

            node->create_interior(*_options);
            _options->residence_service().increase_nodes_allocated();
            return { node_addr, node };
        }
            
        
        static void remove_deleted_nodes(
            std::vector<FarAddress>&& to_remove, std::future<void>&& previous_run, TrieOptions& options)
        {
            auto size_to_remove = to_remove.size();
            if (previous_run.valid()) //complete previous cycle of GC
                previous_run.get();
            OP::vtm::TransactionGuard op_g(
                options.transaction_service().begin_transaction(), true);
            for (const auto& node_addr : to_remove) {
                auto wr_node = options.memory_service().access<node_t>(node_addr);
                auto& node_ref = *wr_node;
                assert(node_ref.magic_word_c == node_ref.expected_magic_word);
                node_ref.destroy_interior(options);
                options.allocator_service().deallocate(node_addr, sizeof(TrieNode));
            }
        }

        /** Ensures that nodes at address specified in vector `to_remove` will be destroyed at transaction end on background 
        * 
        */
        void trigger_remove_node(std::vector<FarAddress> to_remove)
        {
            auto size_to_remove = to_remove.size();
            _options->allocator_service().deallocate(std::move(to_remove));
            _options->residence_service().increase_nodes_allocated(
                -static_cast<std::int64_t>(size_to_remove)
            );
        }

        /** Find first mismatch between the trie path starting at the current position of `iter`
        *   and the input string specified by [`begin`, `end`).
        *
        *   The method walks the trie node-by-node, consuming one byte from the input string per
        *   node level and then comparing the optional per-entry "stem" (a compressed suffix stored
        *   inline in the node) against the remaining input characters.
        *
        *   At every step the iterator `iter` is kept up-to-date so that callers can inspect the
        *   exact trie position where the comparison diverged.
        *
        *   @param iter  In/out iterator that on entry points to the node from which the walk
        *                starts.  On exit it reflects the deepest position reached.
        *   @param begin In/out iterator of the input string.  Advanced as characters are consumed.
        *   @param end   Past-the-end sentinel of the input string.
        *   @return      A `StemCompareResult` value that describes the relationship between the
        *                input string and the trie path at the point where the walk stopped:
        *                - `equals`     – input and trie path are identical.
        *                - `string_end` – input was exhausted before the stem ended (input is a
        *                                 proper prefix of a stored key).
        *                - `stem_end`   – stem was exhausted but input still has characters left
        *                                 (stored key is a proper prefix of the input).
        *                - `unequals`   – a character mismatch was found inside a stem.
        *                - `no_entry`   – the current node has no entry for the consumed byte.
        */
        template <class AtomIterator>
        StemCompareResult mismatch(iterator& iter, AtomIterator& begin, AtomIterator end) const
        {
            using node_data_t = typename node_t::NodeData;
            StringMemoryManager string_memory_manager(*_options);

            StemCompareResult result = StemCompareResult::equals;
            FarAddress node_addr;
            if (iter.is_end())
            {
                node_addr = _root;
                iter.push(address(_root));
            }
            else
                node_addr = iter.rat().address();

            // Each outer iteration descends one level: consume one byte from the input
            // string as the node-level key, then compare the optional stem stored for
            // that key against the remaining input characters.
            while (begin != end
                && !node_addr.is_nil()
                && StemCompareResult::equals == result)
            {
                auto ro_node = 
                    _options->memory_service().view<node_t>(node_addr);
                const auto& node = *ro_node;
                assert(node.magic_word_c == node_t::expected_magic_word);

                // consume one byte as the node-level key ---
                const atom_t step_key = *begin;
                const bool has_child = node.has_child(step_key);
                const bool has_value = node.has_value(step_key);
                // early-exit when the key byte is absent from this node ---
                if (!has_value && !has_child) // strongly equivalent to check step_key == current
                    return StemCompareResult::no_entry;
                ++begin;

                // Record the current position in the iterator (stem size is not yet known).
                iter.rat(
                    key(step_key),
                    terminality(
                        (has_value ? Terminality::term_has_data : Terminality::term_no)
                        | (has_child ? Terminality::term_has_child : Terminality::term_no)),
                    node_version(node._version)
                );


                // compare the stem stored for `step_key` against the input
                result = node.rawc(*_options, step_key,
                    [&](const node_data_t& node_data) -> StemCompareResult
                    {
                        // Keep the child address for the next outer iteration.
                        node_addr = node_data._child;
                        if (node_data._stem.is_nil())
                        {
                            // If input still has characters the caller must descend further.
                            return (begin == end) ? StemCompareResult::equals : StemCompareResult::stem_end;
                        }
                        //if (begin == end) //redundant, but allows avoid stem reading
                        //{
                        //    // Input exhausted exactly at this node level (no stem to compare more).
                        //    return StemCompareResult::string_end;
                        //}
                        // Stem exists: iterate all characters against the input.
                        StemCompareResult stem_result = StemCompareResult::equals;
                        const auto consumed_stem_len = string_memory_manager.get(
                            node_data._stem,
                            [&](atom_t stem_char) -> bool
                            {
                                if (begin == end)
                                {
                                    stem_result = StemCompareResult::string_end;
                                    return false; // stop
                                }
                                if (stem_char != *begin)
                                {
                                    stem_result = StemCompareResult::stem_neq;
                                    return false; // stop – character mismatch
                                }
                                iter._prefix.push_back(*begin++);
                                return true;      // continue – characters matched
                            });
                        iter.rat(chunk_size_plus(static_cast<vtm::fast_dim_t>(consumed_stem_len)));
                        if (result == StemCompareResult::string_end && has_value)
                        {
                            return StemCompareResult::equals;
                        }

                        // Stem was fully consumed without divergence: determine what comes next.
                        if (stem_result == StemCompareResult::equals)
                        {
                            if (begin != end) // begin has been changed in stem
                                return StemCompareResult::stem_end; // more input remains
                            if (!has_value)
                                return StemCompareResult::string_end; // input ended inside a non-terminal
                            // Both stem and input ended simultaneously at a value node.
                        }
                        return stem_result;
                    });

                // if the stem was fully consumed and a child exists,
                //  push the current node onto the iterator stack and
                //  continue the outer loop with the child node. ---
                if (result == StemCompareResult::stem_end && has_child)
                {
                    iter.push(address(node_addr));
                    result = StemCompareResult::equals; //reset for the next cycle
                }
            }

            return result;
        }

        size_t update_impl(iterator& pos, value_type value)
        {
            const auto& back = pos.rat();
            const auto up_key = pos.rat_key();
            assert(all_set(back.terminality(), Terminality::term_has_data));

            auto wr_node = _options->memory_service().access<node_t>(back.address());
            wr_node->raw(*_options, up_key, [&](auto& node_data) {
                wr_node->set_raw_factory_value(*_options, up_key, node_data, [&](auto& dest) {
                    storage_converter_t::reassign(*_options, value, dest);
                    });
                });

            pos.rat(node_version(wr_node->_version));
            _options->residence_service().increase_version();
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
        * \return false if value already exists, true if new value has been added
        */
        template <class AtomIterator, class FValueFactory>
        bool insert_impl(
            iterator& iter, AtomIterator begin, AtomIterator end, FValueFactory&& value_factory)
        {
            StringMemoryManager string_manager(*_options);
            FarAddress node_addr{};
            // resolve address where iterator points
            if (iter._prefix.empty()) //points nowhere
            {
                node_addr = _root;
            }
            else //points on some node
            {
                auto& back = iter.rat();
                auto step_key = iter.rat_key();
                //temp load of parent node
                auto node = _options->memory_service().view<node_t>(back.address());
                //now take the node from the pointed key
                const bool has_child = node->has_child(step_key);
                if (has_child) 
                    node_addr = node->get_child(*_options, step_key);
            }

            vtm::segment_pos_t full_stem_len = 0;
            key_t carry_over_stem;

            while (begin != end && !node_addr.is_nil())
            { // match input key:[begin, end) with current node at node_addr
                auto ro_node = _options->memory_service().view<node_t>(node_addr);
                const auto& node = *ro_node;
                assert(node.magic_word_c == node_t::expected_magic_word);
                const atom_t step_key = *begin;
                const bool has_child = node.has_child(step_key);
                const bool has_value = node.has_value(step_key);
                iter.push(
                    key(step_key),
                    address(node_addr),
                    //-> no need chunk_size(1), as key() does ++ // (1) stands for step_key
                    terminality( //terminality may not indicate real state yet, since slot may contain stem
                        (has_value ? Terminality::term_has_data : Terminality::term_no)
                        | (has_child ? Terminality::term_has_child : Terminality::term_no)),
                    node_version(node._version)
                );
                ++begin;
                if (!has_child && !has_value) // this is strong equivalent to compare `*begin == iter.rat_key()`
                {//when nothing at this point it means node doesn't contains char specified by `begin`, it is exact ins point
                    break;
                }
                typename node_t::stem_str_address_t stem;
                // go over node internals, including: stem, child_addr
                node.rawc(*_options, step_key, [&](const auto& current) {
                        node_addr = current._child;// Keep the child address for the next outer iteration. (may be nil)
                        stem = current._stem;
                    });
                if (stem.is_nil())
                    continue; //ready to follow the next child
                //prepare stem accumulator
                full_stem_len = 0;
                carry_over_stem.clear();
                NullableAtom missed_key;
                string_manager.get(stem, [&](atom_t stem_char) -> bool {//travel over stem
                    ++full_stem_len;
                    if (!carry_over_stem.empty()) //already marked to truncate
                    {
                        carry_over_stem.push_back(stem_char);
                    }
                    else
                    {
                        if (begin == end)
                        {
                            carry_over_stem.push_back(stem_char);
                            return true; //continue stem char consuming
                        }
                        if (stem_char != *begin) //assume begin != end
                        {
                            carry_over_stem.push_back(stem_char);
                            missed_key = *begin;
                        }
                        else
                        {
                            iter.rat(key(*begin));//it doesn't set the key, instead it append char to prefix and increase _stem_size
                        }
                        ++begin;
                    }
                    return true; //check next stem char
                    });
                if (!carry_over_stem.empty()) //any mismatch in stem means break `while`
                {//break the stem
                    using node_data_t = typename node_t::NodeData;
                    auto parent_address = iter.rat().address();
                    //prepare last node to write
                    auto wr_parent = _options->memory_service().access<node_t>(parent_address);
                    auto& parent_node = *wr_parent;
                    node_data_t& parent_data = *parent_node.raw(*_options, step_key, [&](node_data_t& wr_node_data) -> node_data_t* {
                        return &wr_node_data;
                        });
                    string_manager.truncate(parent_data._stem,
                        full_stem_len - static_cast<OP::vtm::segment_pos_t>(carry_over_stem.size()));

                    auto [branch_node_addr, branch_node] = new_node(iter._prefix.size() + 1);
                    auto new_child_key = carry_over_stem[0];
                    //now insert the carry-over part of stem
                    branch_node->insert(*_options, new_child_key,
                        [&](auto& node_entry, bool& out_has_value, bool& out_has_children) {
                            if (carry_over_stem.size() > 1)//[1..end)
                            {
                                node_entry._stem =
                                    string_manager.smart_insert(carry_over_stem.begin() + 1, carry_over_stem.end());
                            }
                            if (has_child)
                            {//move child data (if exists) from current node to the new one
                                out_has_children = true;
                                node_entry._child = node_addr;
                                //don't clear child presence there, do it later(otherwise node will destroy interior)
                            }
                            if (has_value)
                            {//move value (if exists)
                                out_has_value = true;
                                //as soon _value_presence cleared, twice destructor wouldn't called
                                std::swap(node_entry._value, parent_data._value);
                                //will call parent_node->_value_presence.clear later (otherwise node will destroy interior)
                            }
                        });

                    //override child address
                    parent_node.set_raw_child(step_key, parent_data, branch_node_addr);
                    if (has_value) //clear value as it been moved out
                    {
                        OP::rawbits::clear(parent_node._value_presence, step_key);
                        iter.rat(//iterator doesn't point data anymore as it was moved to branch node, but child exists
                            terminality(Terminality::term_has_child)
                        );
                    }
                    iter.rat(node_version(parent_node._version));
                    if (missed_key)
                    { //missed key indicates that new node should be used for following insertion
                        iter.push(
                            address(branch_node_addr),
                            key(missed_key.value())
                        );
                    }
                    node_addr = branch_node_addr;
                    break; // mismatch in stem means we found insert point
                }
            }//while


            if (begin != end)
            {
                if (node_addr.is_nil()) //need promote new child
                {
                    auto parent_address = iter.rat().address();
                    auto parent_node = _options->memory_service().access<node_t>(parent_address);
                    auto step_key = iter.rat_key();
                    auto [child_addr, _] = new_node(iter._prefix.size() + 1);
                    parent_node->set_child(*_options, step_key, child_addr);
                    iter.rat(terminality_or(Terminality::term_has_child));
                    iter.push(address(child_addr), key(*begin));
                    ++begin;
                }
                else // child node is already in stack
                {
                    //iter.rat(key(*begin++)); 
                }

            }
            else // begin == end when string just points to node or stem just in the middle
            {
                if (iter.rat().terminality() & Terminality::term_has_data)
                    return false;//entry already exists
            }
            
            node_addr = iter.rat().address();
            auto wr_access = _options->memory_service().access<node_t>(node_addr);
            auto& wr_node = *wr_access;

            const auto key_of_insert = iter.rat_key();
            if (wr_node.has_child(key_of_insert) && !wr_node.has_value(key_of_insert))
            { //node-entry exists so just assign the value to empty slot
                wr_node.raw(*_options, key_of_insert, [&](auto& node_entry) {
                    assign_value(node_entry._value, std::forward<FValueFactory>(value_factory));
                    OP::rawbits::set(wr_node._value_presence, key_of_insert);
                    });

            }
            else //no entry, make insert...
            {
                wr_node.insert(*_options, key_of_insert,
                    [&](auto& node_entry, bool& out_has_value, bool& out_has_children) {
                        if (begin != end)
                        {
                            assert(std::distance(begin, end) <
                                std::numeric_limits<dim_t>::max() - 1);
                            assert(std::distance(begin, end) > 0);
                            node_entry._stem = string_manager.smart_insert(begin, end);
                        }
                        assign_value(node_entry._value, std::forward<FValueFactory>(value_factory));
                        out_has_value = true;
                    });
            }
            iter.rat(terminality_or(Terminality::term_has_data), node_version(wr_node._version));
            iter.update_stem(begin, end);
            _options->residence_service()
                .increase_count()
                .increase_version();
            return true;//brand new entry
        }

        template <class FValuseFactory>
        void assign_value(typename node_t::data_storage_t& storage, FValuseFactory&& value_factory)
        {
            //payload_manager_t::make_new(
            //    *_topology, storage, value_factory);
            payload_manager_t::allocate(*_options, storage);
            payload_manager_t::raw(*_options, storage, [&](auto& raw_payload) {
                storage_converter_t::serialize(
                    *_options, value_factory(), raw_payload
                );
                });
        }

        iterator erase_impl(iterator& pos, size_t* count = nullptr)
        {
            auto result{ pos };
            _next(result);
            bool erase_dangled_child = false;
            std::vector<FarAddress> nodes_to_remove;
            auto nodes_to_remove_inserter = OP::common::filter_back_inserter(nodes_to_remove,
                [this](const FarAddress& addr) { //prevent removing of root 
                    return (addr.address != this->_root);
                });
            FarAddress back_address{};
            for (bool first = true; pos.node_count(); pos.pop(), first = false)
            {
                const auto& back = pos.rat();
                const auto back_key = pos.rat_key();
                back_address = back.address();
                auto wr_node = _options->memory_service().access<node_t>(back_address);
                auto& node = *wr_node;

                if (!node.erase(*_options, back_key,
                    first/*if needs to remove value*/,
                    erase_dangled_child/*if needs to remove child*/,
                    nodes_to_remove_inserter))
                {//don't need to propagate by iterator up
                    //pos.rat(
                    //    terminality(node.get_state(back_key)),
                    //    node_version(node._version));
                    erase_dangled_child = false;
                    break;
                }
                //there since entire empty node can be deleted, do it on next step. If it is top-most
                //root - it must be kept even in empty state
                erase_dangled_child = true;
            }
            if (erase_dangled_child && !back_address.is_nil())
                *nodes_to_remove_inserter++ = back_address;
            //process totally empty nodes
            trigger_remove_node(std::move(nodes_to_remove));                
            _options->residence_service().increase_version().increase_count(-1);
            if (count) { ++*count; }
            return result;
        }

        size_t prefixed_erase_all_impl(iterator& prefix, bool erase_prefix = true)
        {
            const auto& back = prefix.rat();
            auto rat_key = prefix.rat_key();
            auto rat_terminality = back.terminality();
            auto rat_addr = back.address();
            iterator local_iter = prefix;
            _next(prefix, /*way_down*/false);
            size_t erased_terminals = 0;

            std::vector<FarAddress> to_process;
            auto to_remove_inserter = std::back_inserter(to_process); //root cannot appear there, so don't need filter_back_inserter
            
            auto wr_node = _options->memory_service().access<node_t>(rat_addr);
            bool erase_dangled_child = wr_node->erase(*_options, rat_key,
                /*erase_data*/erase_prefix && (rat_terminality & Terminality::term_has_data),
                /*erase_child*/(rat_terminality & Terminality::term_has_child),
                to_remove_inserter);
            if(erase_prefix && (rat_terminality & Terminality::term_has_data))
                ++erased_terminals;
            for(local_iter.pop(); erase_dangled_child && local_iter.node_count(); local_iter.pop())
            { // go up if pointed by prefix node is empty
                const auto& back = local_iter.rat();
                const auto back_key = local_iter.rat_key();
                ;
                auto wr_super = _options->memory_service().access<node_t>(back.address());
                erase_dangled_child = wr_super->erase(*_options, back_key,
                    /*erase_data*/false,
                    /*erase_child*/true,
                    to_remove_inserter);
            }

            //`to_process` still can be empty
            for (size_t top = 0; top < to_process.size(); ++top) //index used to keep stability relative to updating vector
            {
                auto wr_node = _options->memory_service().access<node_t>(to_process[top]);
                erased_terminals += wr_node->erase_all(*_options, to_remove_inserter);
            }
            
            //auto parent_wr_node = _options->memory_service().access<node_t>(back.address());

            //FarAddress top_to_remove{};
            //if (parent_wr_node->erase(*_options, rat_key, false/*no data erased*/, true/*erase child**/, to_remove_inserter))
            //{//this node is empty as well, remove it
            //    top_to_remove = back.address(); //add this address to `to_process` after all
            //}
            // //includes one referencing 
            ////use index based iteration, since `to_remove_inserter` appends to result vector
            //for(size_t i = 0; i < to_process.size(); ++i)
            //{
            //    const auto& node_addr = to_process[i];
            //    auto wr_node = _options->memory_service().access<node_t>(node_addr);
            //    erased_terminals += wr_node->erase_all(*_options, to_remove_inserter);
            //}
            //need additional variable since #erase will decrement counter as well
            auto effective_decrease_number = -static_cast<std::make_signed_t<size_t>>(erased_terminals);

            //const auto& new_back = prefix.rat(
            //    terminality_and(~Terminality::term_has_child));//avoid way-down
            //if (erase_prefix && all_set(new_back.terminality(), Terminality::term_has_data))
            //{
            //    size_t counter = 0;
            //    prefix = erase_impl(prefix, &counter);
            //    assert(counter);//otherwise terminality flag must be altered
            //    erased_terminals += counter;
            //}
            //else if (!prefix.is_end()) //need position next, only if erase_impl wasn't called 
            //{
            //    prefix.rat(
            //        terminality_and(~Terminality::term_has_child));
            //    _next(prefix);
            //}
            //if (to_process[0] == this->_root)
            //    to_process.erase(to_process.begin()); //don't include root to erasure scope

            trigger_remove_node(std::move(to_process));
            _options->residence_service()
                .increase_version()
                .increase_count(effective_decrease_number); //reduce number of terminals

            return erased_terminals;
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
                const auto back_key = result_iter.rat_key();
                auto ro_node = _options->memory_service().view<node_t>(back.address());
                next_address = ro_node->get_child(*_options, static_cast<atom_t>(back_key));
            }
            result_iter.push(
                address(next_address)
            );
            return true;
        }

        /**Return not fully valid iterator that matches common part specified by [begin, end)
        * \return reason why iterator unmatched to query string
        */
        template <class Atom>
        StemCompareResult common_prefix(Atom& begin, Atom end, iterator& result_iter) const
        {
            StemCompareResult retval = StemCompareResult::no_entry;
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
            const auto back_key = result.rat_key();
            FarAddress child = back.address();
            auto ro_node = _options->memory_service().view<node_t>(child);
            child = ro_node->get_child(*_options, back_key);
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

            auto mismatch_result = mismatch(prefix, begin, aend);
            switch (mismatch_result)
            {
            case StemCompareResult::equals: //exact match
                return prefix.rat().terminality() & Terminality::term_has_data;
            case StemCompareResult::no_entry:
            {
                auto back = prefix.rat(chunk_size(0));//not a reference
                assert(begin != aend); // string is not empty because `mismatch` has indicated absence of `*begin`
                auto back_key = *begin;
                auto [ok, child] = load_iterator(
                    back.address(), prefix,
                    [&](const node_t& ro_node)
                    { //on absence of entry just try find bigger in the same node
                        return ro_node.next(back_key);
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
                        [](const node_t& ro_node) { return ro_node.first(); });
                }
                return false; //not an exact match
            }
            case StemCompareResult::string_end:
            {
                auto [ok, child] = load_iterator(
                    prefix.rat().address(), prefix,
                    [&](const node_t&) {
                        return NullableAtom{ prefix.rat_key() };
                    },
                    &iterator::update_back);
                assert(ok); //empty node is impossible there
                if (is_not_set(prefix.rat().terminality(), Terminality::term_has_data))
                {
                    enter_deep_until_terminal(child, prefix,
                        [](const node_t& ro_node) {
                            return ro_node.first();
                        });
                }
                return false;//not an exact match
            }
            case StemCompareResult::stem_neq:
                [[fallthrough]];
            case StemCompareResult::stem_end:
                break; //just follow

            }
            //when: StemCompareResult::stem_end || StemCompareResult::string_end ||
            //   ( StemCompareResult::unequals && !iter.is_end())
            _next(prefix);
            return false; //not an exact match
        }

        /**Sync iterator to reflect current version of trie
        * \return bool where true means success sync (or no sync was needed) and second string has no sense,
        *         while false means inability to sync (entry was erased);
        * \param fallback atomic_string_t that set overall result false origin key of iterator, because on
        *       unsuccessful sync at exit iterator is damaged. Pointer may be omitted if you don't need recovery
        */
        bool sync_iterator(iterator& iter, key_t* fallback = nullptr) const
        {
            const std::uint64_t this_ver = this->version();
            if (this_ver == iter.version()) //no sync is needed
                return true;

            iter._version = this_ver;
            size_t node_order = 0;
            size_t prefix_length = 0;
            //take each node of iterator and check against current version of real node
            for (const auto& i : iter._position_stack)
            {
                auto ro_data = _options->memory_service().view<node_t>(i.address());
                const auto& node = *ro_data;
                if (node._hash_table.is_nil())
                {//may be iterator so old, so node has been removed
                    break;
                }
                if (node._version != i.version())
                {
                    //in ideal world I'd check terminality and prefix equality, but it is longer than just reload iterator
                    break;
                }
                prefix_length += i.chunk_size();
                ++node_order;
            }
            if (prefix_length == iter._prefix.size())
                return true; //no changes
            if (fallback)
            {
                *fallback = iter.key(); //need make copy since next instructions corrupt the iterator
            }
            auto repeat_search_key =
                iter.key().substr(prefix_length); //cut prefix str (make copy)
            iter._prefix.resize(prefix_length);
            iter._position_stack.erase( //cut the invalid part of stack
                iter._position_stack.begin() + node_order,
                iter._position_stack.end());
            auto suffix_begin = repeat_search_key.begin();
            auto suffix_end = repeat_search_key.end();
            return lower_bound_impl(suffix_begin, suffix_end, iter);
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
            FFindEntry&& pos_locator, FIteratorUpdate&& iterator_update) const
        {
            auto ro_node = _options->memory_service().view<node_t>(node_addr);
            NullableAtom pos = pos_locator(*ro_node);
            if (!pos)
            { //no first
                return std::make_pair(false, FarAddress());
            }

            return ro_node->rawc(*_options, pos.value(),
                [&](const auto& node_data) {
                    (dest.*iterator_update)(position_t(
                        address(node_addr),
                        node_version(ro_node->_version)
                    ));
                    dest._prefix.push_back(pos.value()); //assign key
                    vtm::fast_dim_t stem_len = 1;//stands for key
                    if (!node_data._stem.is_nil())
                    {//if stem exists should be placed to iterator
                        StringMemoryManager smm(*_options);
                        stem_len += smm.append_to(node_data._stem, dest._prefix);
                    }

                    dest.rat(
                        chunk_size(stem_len),
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
                const auto rat_key = i.rat_key();
                //try enter deep
                if (way_down && all_set(back.terminality(), Terminality::term_has_child))
                {   //get address of child
                    auto ro_node = _options->memory_service().view<node_t>(back.address());

                    auto child_addr = ro_node->get_child(*_options, rat_key);
                    enter_deep_until_terminal(child_addr, i,
                        [](const node_t& ro_node) {
                            return ro_node.first();
                        });
                    return;
                }
                //try navigate right from current position
                auto [ok, child] = load_iterator(i.rat().address(), i,
                    [&i](const node_t& node)
                    {
                        //don't optimize `i.rat` since i may change
                        return node.next(i.rat_key());
                    },
                    &iterator::update_back);
                if (ok)
                { //navigation right succeeded
                    if (all_set(i.rat().terminality(), Terminality::term_has_data))
                    {//i already points to correct entry
                        return;
                    }
                    enter_deep_until_terminal(child, i,
                        [](const node_t& node) { return node.first(); });
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

    
}//ns:OP::trie

#endif //_OP_TRIE_TRIE__H_
