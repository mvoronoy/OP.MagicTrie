#pragma once
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
//#include <op/common/NamedArgs.h>
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
        /**Implement functor for subrange method to implement predicate that detects end of range iteration*/
        struct StartWithPredicate
        {
            StartWithPredicate(atom_string_t && prefix)
                : _prefix(std::move(prefix))
            {
            }
            explicit StartWithPredicate(const atom_string_t& prefix)
                : _prefix(prefix)
            {
            }

            template <class Iterator>
            bool operator()(const Iterator& check) const
            {
                const auto & str = check.key();
                if (str.length() < _prefix.length())
                    return false;
                return std::equal(_prefix.begin(), _prefix.end(), str.begin());
            }
        private:
            atom_string_t _prefix;
        };

        namespace details
        {
            /** Declare SFINAE predicate to test  if class  has member _begin */
            OP_DECLARE_CLASS_HAS_MEMBER( _begin )
            /** Declare SFINAE predicate to test  if class  has member _next */
            OP_DECLARE_CLASS_HAS_MEMBER( _next )
            /** Declare SFINAE predicate to test  if class  has member _in_range */
            OP_DECLARE_CLASS_HAS_MEMBER( _in_range )
            /** Declare SFINAE predicate to test  if class  has member _end */
            OP_DECLARE_CLASS_HAS_MEMBER( _end )
            /** Declare SFINAE predicate to test  if class  has member _lower_bound */
            OP_DECLARE_CLASS_HAS_MEMBER( _lower_bound )

            /** Declare SFINAE predicate to test if class  has member next_lower_bound_of */
            OP_DECLARE_CLASS_HAS_MEMBER( next_lower_bound_of )
            
        }
        

        template <class Trie>
        struct Ingredient
        {
            using start_with_predicate_t = StartWithPredicate;
            
            /** Ingredient for MixAlgorithmRangeAdapter - allows range use `first_child` method instead of `begin` */
            struct ChildBegin
            {
                /** Construct by finding `first_child` from interator. For details see Trie::first_child */ 
                template <class SharedArguments>
                ChildBegin(const SharedArguments& args)
                    : _iterator(std::get<typename Trie::iterator>(args))
                {
                }

                ChildBegin(typename Trie::iterator iter)
                    : _iterator(std::move(iter))
                {
                }

                typename Trie::iterator _begin(const Trie& trie) const
                {
                    return trie.first_child(_iterator);
                }
            private:
                /** Mutable since Trie can update version of iterartor */
                mutable typename Trie::iterator _iterator;
            };
            /** Ingredient for MixAlgorithmRangeAdapter - allows range use `find(key | `first_child` method instead of `begin` */
            struct ChildOfKeyBegin
            {
                /** Construct by finding `first_child` from interator. For details see Trie::first_child */ 
                template <class SharedArguments>
                ChildOfKeyBegin(const SharedArguments& args)
                    : _key(std::get<atom_string_t>(args))
                {
                }
                ChildOfKeyBegin(const atom_string_t& key)
                    : _key(key)
                {
                }

                typename Trie::iterator _begin(const Trie& trie) const
                {
                    auto found = trie.find(_key);
                    if( trie.in_range( found ) )
                        return trie.first_child(found);
                    return trie.end();
                }
            private:
                const atom_string_t _key;
            };

            /** Ingredient for MixAlgorithmRangeAdapter - allows range use `next_sibling` method instead of `next` */
            struct SiblingNext
            {
                void _next(const Trie& trie, typename Trie::iterator& i) const
                {
                    trie.next_sibling(i);
                }
            };


            /** Ingredient for MixAlgorithmRangeAdapter - allows start iteration from first element that matches specific prefix string */
            struct PrefixedBegin
            {
                template <class SharedArguments>
                PrefixedBegin(const SharedArguments& args)
                    : _prefix(std::get<atom_string_t>(args))
                {
                }
                PrefixedBegin(atom_string_t prefix)
                    : _prefix(std::move(prefix))
                {
                }
                typename Trie::iterator _begin(const Trie& trie) const
                {
                    return trie.prefixed_begin(std::begin(_prefix), std::end(_prefix));
                }
            private:
                const atom_string_t _prefix;
            };
            /** Ingredient for MixAlgorithmRangeAdapter - allows start iteration from first element that 
            greater or equal than specific key. If key is not started with prefix:
                - for lexicographic less key - then lower_bound starts from prefix
                - for lexicographic greater key - then lower_bound returns end()
            */
            struct PrefixedLowerBound
            {
                template <class SharedArguments>
                PrefixedLowerBound(const SharedArguments& args)
                    : _prefix(std::get<atom_string_t>(args))
                {
                }
                PrefixedLowerBound(atom_string_t prefix)
                    : _prefix(std::move(prefix))
                {
                }
                typename Trie::iterator _lower_bound(const Trie& trie, const atom_string_t& key) const
                {
                    auto kb = std::begin(key);
                    auto ke = std::end(key);
                    auto pb = std::begin(_prefix);
                    auto pe = std::end(_prefix);
                    //do kind of lexicographic compare to check if key starts with prefix 
                    int cmpres = OP::ranges::str_lexico_comparator(pb, pe, kb, ke);
                    if (pb == pe) // key starts from prefix
                    {
                        return trie.lower_bound(key);
                    }
                    if( cmpres > 0 )
                        return trie.lower_bound(_prefix); //because key is less than _prefix => find from prefix
                    return trie.end();
                }
            private:
                const atom_string_t _prefix;
            };


            /** Ingredient for MixAlgorithmRangeAdapter - allows range customize `in_range` in a way to check that 
            * range iterates over items with specific prefix */
            struct PrefixedInRange
            {
                /**Construct with shared tuple, expects that `start_with_predicate_t ` exists*/
                template <class SharedArguments>
                PrefixedInRange(const SharedArguments& args)
                    : _prefix_check(std::get<start_with_predicate_t>(args))
                    {}

                PrefixedInRange(start_with_predicate_t pred)
                    : _prefix_check(std::move(pred))
                    {}

                bool _in_range(const Trie& trie, const typename Trie::iterator& i) const
                {
                    return trie.in_range(i) && _prefix_check(i);
                }
            private:
                const start_with_predicate_t _prefix_check;
            };

            /** Ingredient for MixAlgorithmRangeAdapter - just alias for PrefixedInRange, but give more consistence when mixing iteration over
            * children range like: <ChildBegin, ChildInRange, SiblingNext>
            */
            using ChildInRange = PrefixedInRange;

            /** Ingredient for MixAlgorithmRangeAdapter - implments `in_range` with extra predicate */
            struct TakeWhileInRange
            {
                /** extract predicate from shared argument std::tuple from */
                template <class SharedArguments>
                TakeWhileInRange(const SharedArguments& args) noexcept
                    : _predicate(std::get<bool(const typename Trie::iterator&)>(args)) 
                {}

                TakeWhileInRange(bool(*predicate)(const typename Trie::iterator&)) noexcept
                    : _predicate(std::move(predicate))
                {}

                bool _in_range(const Trie& trie, const typename Trie::iterator& i) const
                {
                    return trie.in_range(i) && _predicate(i);
                }

            private:
                const std::function< bool(const typename Trie::iterator&)> _predicate;
            };

            /** Ingredient for MixAlgorithmRangeAdapter - implments `find` to play role of begin */
            struct Find
            {
                template <class SharedArguments>
                Find(const SharedArguments& args)
                    : _key(std::get<atom_string_t>(args))
                {}

                typename Trie::iterator _begin(const Trie& trie) const
                {
                    return trie.find(_key);
                }

            private:
                const atom_string_t _key;
            };

        };

        template <class Trie, class ... Mx>
        struct Mixer;


        template <class Trie>
        struct Mixer<Trie> 
        {
            explicit Mixer() = default;
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

            void _next_lower_bound_of(const Trie& trie, typename Trie::iterator& i, const atom_string_t& k) const
            {
                trie.next_lower_bound_of(i, k);
            }

        };
        /** class aggregates together all algorithms */
        template <class Trie, class M, class ... Mx>
        struct Mixer<Trie, M, Mx...> : public M, public Mixer<Trie, Mx ... >
        {
            using base_t = Mixer<Trie, Mx ... >;
            using this_t = Mixer<Trie, M >;

            /** Constructor suppose Ingredient::???? accept tuple parameter*/
            template < typename ...  Args,
                    typename = std::enable_if_t< !std::is_default_constructible<M>::value >
            >
            explicit Mixer(const std::tuple<Args ...>& arguments) noexcept
                : M(arguments)
                , base_t(arguments)
            {
            }

            template <typename ...  Args,
                typename = std::enable_if_t< std::is_default_constructible<M>::value >
            >
            explicit Mixer(const std::tuple<Args ...>& arguments, ...) noexcept
                : base_t(arguments)
            {}

            explicit Mixer(M && m, Mx&& ... mx) noexcept
                : M (std::forward<M>(m))
                , base_t(std::forward<Mx>(mx)...)
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
        struct MixAlgorithmRangeAdapter : public OP::ranges::OrderedRangeOptimizedJoin< typename TTrie::key_t, typename TTrie::value_t>
        {
            using trie_t = TTrie;
            using mixer_t = Mixer<trie_t, Mx ...>;
            using base_t = OP::ranges::OrderedRangeOptimizedJoin< typename TTrie::key_t, typename TTrie::value_t>;
            using range_t = typename base_t::range_t;
            using mixer_iterator_t = typename trie_t::iterator;
            using iterator = typename base_t::iterator;
            using key_t = typename base_t::key_t;
            using value_t = typename base_t::value_t;

            MixAlgorithmRangeAdapter(std::shared_ptr<const trie_t> parent) noexcept
                : base_t( [](const key_t& left, const key_t& right) -> int{ return left.compare(right);})
                , _parent(std::move(parent))
                , _mixer{}
            {
            }

            template <typename ...  Args>
            MixAlgorithmRangeAdapter(std::shared_ptr<const trie_t> parent, Args&& ... args) noexcept
                : base_t( [](const key_t& left, const key_t& right) -> int{ return left.compare(right);})
                , _parent(std::move(parent))
                , _mixer(std::make_tuple(std::forward<Args>(args)...))
            {
            }

            MixAlgorithmRangeAdapter(std::piecewise_construct_t _, std::shared_ptr<const trie_t> parent, Mx&& ... args) noexcept
                : base_t( [](const key_t& left, const key_t& right) -> int{ return left.compare(right);})
                , _parent(std::move(parent))
                , _mixer(std::forward<Mx>(args)...)
            {
            }

            iterator begin() const override
            {
                return iterator( std::const_pointer_cast<range_t const>(this->shared_from_this()),
                    payload_t::factory(std::move(_mixer._begin(*_parent))));
            }
           
            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                return _mixer._in_range(*_parent, check.OP_TEMPL_METH(impl)< payload_t >()._mixer_it );
            }
            void next(iterator& pos) const override
            {
                return _mixer._next(*_parent, pos.OP_TEMPL_METH(impl)< payload_t >()._mixer_it);
            }

            iterator lower_bound(const key_t& key) const override
            {
                return iterator(std::const_pointer_cast<range_t const>(this->shared_from_this()),
                    payload_t::factory(std::move(_mixer._lower_bound(*_parent, key))));
            }
            
            void next_lower_bound_of(iterator& i, const key_t& k) const override
            {
                _mixer._next_lower_bound_of(*_parent, i.OP_TEMPL_METH(impl)< payload_t >()._mixer_it, k);
            }

            const std::shared_ptr<const trie_t>& get_parent() const
            {
                return _parent;
            }

        protected:
            struct payload_t : public iterator::RangeIteratorImpl
            {
                static std::unique_ptr<typename iterator::RangeIteratorImpl> factory(mixer_iterator_t mixer_it)
                {
                    return std::unique_ptr<typename iterator::RangeIteratorImpl>(new payload_t(std::move(mixer_it)));
                }
                payload_t(mixer_iterator_t mixer_it)
                    :_mixer_it(std::move(mixer_it))
                {}
                
                const typename trie_t::key_t& key() const override
                {
                    return _mixer_it.key();
                }
                const typename trie_t::value_t& value() const override
                {
                    _ref_value_pack = _mixer_it.value();
                    return _ref_value_pack;
                }
                std::unique_ptr<typename iterator::RangeIteratorImpl> clone() const override
                {
                    return factory(_mixer_it);
                }
                mixer_iterator_t _mixer_it;
                mutable typename trie_t::value_t _ref_value_pack;
            };
            /** Method compiled only when class Trie supported next_lower_bound_of */
            template <class Str, class U = std::enable_if_t<OP_CHECK_CLASS_HAS_MEMBER(TTrie, next_lower_bound_of)> >
            void _next_lower_bound_of(typename TTrie::iterator& i, const Str& key) const
            {
                _parent->next_lower_bound_of(i, key);
            }


        private:
            std::shared_ptr<const trie_t> _parent;
            mixer_t _mixer;
            
        };
        
        template <class TTrie, class ... Mx>
        std::shared_ptr< MixAlgorithmRangeAdapter<TTrie, Mx...> const>
        make_mixed_range(std::shared_ptr<const TTrie> trie, Mx&& ...args)
        {
            using mixed_t = MixAlgorithmRangeAdapter<TTrie, Mx...>;
            return std::make_shared<mixed_t>(std::piecewise_construct, std::move(trie), std::forward<Mx>(args)...);
        }


    } //ns:trie
}//ns:OP
#undef OP_DECLARE_CLASS_HAS_MEMBER
#undef OP_CHECK_CLASS_HAS_MEMBER
