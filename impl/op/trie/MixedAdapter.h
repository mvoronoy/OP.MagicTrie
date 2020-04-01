#pragma once
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <op/trie/TrieRangeAdapter.h>
#include <op/common/NamedArgs.h>
#include <memory>
#include <functional>
#include  <type_traits>
#define OP_DECLARE_CLASS_HAS_MEMBER(Member)  template <typename A>\
    class has_##Member \
    { \
        typedef char YesType[1]; \
        typedef char NoType[2]; \
        template <typename C> static YesType& test( decltype(&C::Member) ) ; \
        template <typename C> static NoType& test(...); \
    public: \
        enum { value = sizeof(test<A>(nullptr)) == sizeof(YesType) }; \
    };
/** Complimentar to OP_DECLARE_CLASS_HAS_MEMBER - generate compile time const that indicate if class `A` has member `Member`*/
#define OP_CHECK_CLASS_HAS_MEMBER(Class, Member) (details::has_##Member<Class>::value)

namespace OP
{
    namespace trie
    {
        namespace details
        {
            /** Declare SFINAE predicate to test  if class  has member _begin */
            OP_DECLARE_CLASS_HAS_MEMBER( _begin )
            /** Declare SFINAE predicate to test  if class  has member _next */
            OP_DECLARE_CLASS_HAS_MEMBER( _next )
            /** Declare SFINAE predicate to test  if class  has member _in_range */
            OP_DECLARE_CLASS_HAS_MEMBER( _in_range )
            /** Declare SFINAE predicate to test  if class  has member _end */
            OP_DECLARE_CLASS_HAS_MEMBER(_end)
            /** Declare SFINAE predicate to test  if class  has member _lower_bound */
            OP_DECLARE_CLASS_HAS_MEMBER(_lower_bound)
        }
        

        template <class Trie>
        struct Ingredient
        {
            static constexpr OP::utils::ArgId<1> nm_prefix_string_c{};
            static constexpr OP::utils::ArgId<2> nm_prefix_check_predicate_c{};

            /*struct DefaultBegin
            {
            protected:
                typename Trie::iterator _begin(Trie& trie) const
                {
                    return trie->begin();
                }
            };
            struct DefaultNext
            {
            protected:
                void _next(Trie& trie, typename Trie::iterator& i) const
                {
                    trie->next(i);
                }
            };
            struct DefaultInRange
            {
            protected:
                bool _in_range(Trie& trie, const typename Trie::iterator& i) const
                {
                    return trie->in_range(i);
                }
            };
             */

            struct ChildBegin
            {

                template <class SharedArguments>
                ChildBegin(const SharedArguments& args)
                    : _prefix(args.get(nm_prefix_string_c))
                {
                }

                typename Trie::iterator _begin(const Trie& trie) const
                {
                    auto found = trie.find(_prefix);
                    if( trie.in_range(found) )
                        return trie.first_child(found);
                    return trie.end();
                }
                const atom_string_t& _prefix;
            };
            struct ChildNext
            {

                void _next(const Trie& trie, typename Trie::iterator& i) const
                {
                    trie.next_sibling(i);
                }
            };

            struct PrefixedInRange
            {
                template <class SharedArguments>
                PrefixedInRange(const SharedArguments& args)
                    : _prefix_check(args.get(nm_prefix_check_predicate_c))
                    {}
                bool _in_range(const Trie& trie, const typename Trie::iterator& i) const
                {
                    return trie.in_range(i) && _prefix_check(i);
                }
                const StartWithPredicate<typename Trie::iterator>& _prefix_check;
            };
            using ChildInRange = PrefixedInRange;
            struct PrefixedLowerBoundRange
            {

                template <class SharedArguments>
                PrefixedLowerBoundRange(const SharedArguments& args)
                    : _prefix_check(args.get(nm_prefix_check_predicate_c))
                {}

                typename Trie::iterator _lower_bound(const Trie& trie, const atom_string_t& key) const
                {
                    auto found = trie.lower_bound(key);
                    if(trie.in_range(found) && _prefix_check(found) )
                        return found;
                    return trie.end();
                }
                const StartWithPredicate<typename Trie::iterator>& _prefix_check;
            };
        };

        template <class Trie, class ... Mx>
        struct Mixer;


        template <class Trie>
        struct Mixer<Trie> 
        {
                template<class SharedArguments>
                explicit Mixer(const SharedArguments& arguments) noexcept{}
                //default impl that invoke Trie::
                typename Trie::iterator _begin(const Trie& trie) const
                {
                    return trie.begin();
                }
                
                void _next(const Trie& trie, typename Trie::iterator& i) const
                {
                    trie.next(i);
                }

                bool _in_range(const Trie& trie, const typename Trie::iterator& i) const
                {
                    return trie.in_range(i);
                }

                typename Trie::iterator _end(const Trie& trie) const
                {
                    return trie.end();
                }

                typename Trie::iterator _lower_bound(const Trie& trie, const atom_string_t& key) const
                {
                    return trie.lower_bound(key);
                } 
        };
        /** class aggregates together all algorithms */
        template <class Trie, class M, class ... Mx>
        struct Mixer<Trie, M, Mx...> : public M, public Mixer<Trie, Mx ... >
        {
            using base_t = Mixer<Trie, Mx ... >;
            using this_t = Mixer<Trie, M >;

            /** Constructor suppose consume 1 arg from input params of type `A` to delegate to Ingredient::???? */
            template <class SharedArguments,
                    typename = std::enable_if_t< !std::is_default_constructible<M>::value >
            >
            explicit Mixer(const SharedArguments& arguments) noexcept
                : M(arguments)
                ,base_t(arguments)
            {
            }

            template <class SharedArguments,
                typename = std::enable_if_t< std::is_default_constructible<M>::value >
            >
            explicit Mixer(const SharedArguments& arguments, ...) noexcept
                : base_t(arguments)
            {}

            ///** Constructor just delegate to manage input params to base classe. In other words `T` constructor has no arguments */
            //template <typename ...  Args,
            //    typename = std::enable_if_t< std::is_default_constructible<M>::value >
            //>
            //explicit Mixer(Args&& ... args) noexcept
            //    : base_t(std::forward<Args>(args)...)
            //{}

            
            using base_begin_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _begin), M, base_t >::type;
            using base_begin_t::_begin;

            using base_next_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _next), M, base_t >::type;
            using base_next_t::_next;
            
            using base_in_range_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _in_range), M, base_t >::type;
            using base_in_range_t::_in_range;

            using base_end_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _end), M, base_t >::type;
            using base_end_t::_end;

            using base_lower_bound_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _lower_bound), M, base_t >::type;
            using base_lower_bound_t::_lower_bound;
        };

        /**
        *   Allows trie to mimic OP::ranges::PrefixRange capabilities
        */
        template <class TTrie, class ... Mx>
        struct MixAlgorithmRangeAdapter : public OP::ranges::OrderedRange< typename TTrie::iterator >
        {
            using trie_t = TTrie;
            using mixer_t = Mixer<trie_t, Mx ...>;
            using iterator = typename trie_t::iterator;

            mixer_t _mixer;
            template <class SharedArguments>
            MixAlgorithmRangeAdapter(std::shared_ptr<const trie_t> parent, const SharedArguments& args) noexcept
                : _parent(std::move(parent))
                , _mixer(args)
            {
            }

            iterator begin() const override
            {
                return _mixer._begin(*_parent);
            }
            iterator end() const 
            {
                return _mixer.end(*_parent);
            }

            bool in_range(const iterator& check) const override
            {
                return _mixer._in_range(*_parent, check);
            }
            void next(iterator& pos) const override
            {
                return _mixer._next(*_parent, pos);
            }

            iterator lower_bound(const typename iterator::key_type& key) const override
            {
                return _mixer._lower_bound(*_parent, key);
            }
            const std::shared_ptr<const trie_t>& get_parent() const
            {
                return _parent;
            }

        private:
            std::shared_ptr<const trie_t> _parent;
        };
        

    } //ns:trie
}//ns:OP
#undef OP_DECLARE_CLASS_HAS_MEMBER
#undef OP_CHECK_CLASS_HAS_MEMBER
