#pragma once
#ifndef _OP_TRIE_MIXEDADAPTER__H_
#define _OP_TRIE_MIXEDADAPTER__H_
#include <op/flur/flur.h>

#include <memory>
#include <functional>
#include <type_traits>
#include <op/common/astr.h>
#include <op/common/has_member_def.h>
#include <op/common/LexComparator.h>


namespace OP::trie
{
    /** Implement predicate checking 'start-with' logic
    */
    template <class AtomString >
    struct StartWithPredicate
    {
        StartWithPredicate(AtomString prefix) noexcept
            : _prefix(std::move(prefix))
        {
        }

        template <class Iterator>
        bool operator()(const Iterator& check) const
        {
            const auto & str = check.key();
            if (str.size() < _prefix.size())
                return false;
            return std::equal(_prefix.begin(), _prefix.end(), str.begin());
        }
    private:
        const std::decay_t<AtomString> _prefix;
    };

    // explicit deduction guide (not needed as of C++20)
    template <class AtomString>
    StartWithPredicate(AtomString) -> StartWithPredicate<AtomString>;

    namespace details
    {
        /** Declare SFINAE predicate to test  if class  has member _begin */
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER( _begin )
        /** Declare SFINAE predicate to test  if class  has member _next */
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER( _next )
        /** Declare SFINAE predicate to test  if class  has member _in_range */
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER( _in_range )
        /** Declare SFINAE predicate to test  if class  has member _end */
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER( _end )
        /** Declare SFINAE predicate to test if class  has member lower_bound */
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER( _lower_bound )
    }
    

    namespace Ingredient
    {
        /** Ingredient for MixAlgorithmRangeAdapter - allows range use `first_child` method instead of `begin` */
        template <class Iterator>
        struct ChildBegin
        {
            ChildBegin(Iterator iter)
                : _iterator(std::move(iter))
            {
            }

            template <class Trie>
            auto _begin(const Trie& trie) const
            {
                return trie.first_child(_iterator);
            }

        private:
            /** Mutable since Trie can update version of iterator */
            mutable std::decay_t<Iterator> _iterator;
        };
        
        template <class Iter> //deduction guide
        ChildBegin(Iter) -> ChildBegin<Iter>;


        /** Ingredient for MixAlgorithmRangeAdapter - allows range use `find(key | `first_child` method instead of `begin` */
        template <class AtomString>
        struct ChildOfKeyBegin
        {
            constexpr explicit ChildOfKeyBegin(AtomString key) noexcept
                : _key(std::move(key))
            {
            }

            template <class Trie>
            auto _begin(const Trie& trie) const
            {
                auto found = trie.find(_key);
                if( trie.in_range( found ) )
                    return trie.first_child(found);
                return trie.end();
            }

        private:
            const std::decay_t<AtomString> _key;
        };

        template <class AtomString> //deduction guide
        ChildOfKeyBegin(AtomString) -> ChildOfKeyBegin<AtomString>;
        

        /** Ingredient for MixAlgorithmRangeAdapter - allows range use `next_sibling` method instead of `next` */
        struct SiblingNext
        {
            template <class Trie>
            void _next(const Trie& trie, typename Trie::iterator& i) const
            {
                trie.next_sibling(i);
            }
        };


        /** Ingredient for MixAlgorithmRangeAdapter - allows start iteration from first element that matches specific prefix string */
        template <class AtomString>
        struct PrefixedBegin
        {

            constexpr explicit PrefixedBegin(AtomString prefix) noexcept
                : _prefix(std::move(prefix))
            {
            }

            template <class Trie>
            auto _begin(const Trie& trie) const
            {
                return trie.prefixed_begin(std::begin(_prefix), std::end(_prefix));
            }

        private:
            const std::decay_t<AtomString> _prefix;
        };

        template <class AtomString> //deduction guide
        PrefixedBegin(AtomString) -> PrefixedBegin<AtomString>;

        /** Ingredient for MixAlgorithmRangeAdapter - allows start iteration from first element that 
        greater or equal than specific key. If key is not started with prefix:
            - for lexicographic less key - then lower_bound starts from prefix
            - for lexicographic greater key - then lower_bound returns end()
        */
        template <class AtomString>
        struct PrefixedLowerBound
        {

            OP_CONSTEXPR_CPP20 explicit PrefixedLowerBound(AtomString prefix) noexcept
                : _prefix(std::move(prefix))
            {
            }

            /** 
            * \param i - [out] result iterator
            */
            template <class Trie>
            void _lower_bound(const Trie& trie, typename Trie::iterator& i, const typename Trie::key_t& key) const
            {
                auto kb = std::begin(key);
                auto ke = std::end(key);
                auto pb = std::begin(_prefix);
                auto pe = std::end(_prefix);
                //do lexicographic compare to check if ever key starts with prefix 
                int cmpres = OP::common::str_lexico_comparator(pb, pe, kb, ke);
                if (pb == pe) // key starts from prefix
                {
                    i = trie.lower_bound(key);
                }
                else
                {
                    if( cmpres > 0 )
                        i = trie.lower_bound(_prefix); //because key is less than _prefix => find from prefix
                    else
                        i = trie.end();
                }
            }

        private:
            const std::decay_t<AtomString> _prefix;
        };
        template <class AtomString> //deduction guide
        PrefixedLowerBound(AtomString) -> PrefixedLowerBound<AtomString>;

        /** Ingredient for MixAlgorithmRangeAdapter - allows range customize `in_range` in a way to check that 
        * range iterates over items with specific prefix 
        * \tparam F should be `bool(const iterator&)`
        */
        template <class F>
        struct PrefixedInRange
        {
            using functor_t = std::decay_t<F>;

            explicit PrefixedInRange(F pred) noexcept
                : _prefix_check(std::forward<F>(pred))
                {}

            template <class Trie>
            bool _in_range(const Trie& trie, const typename Trie::iterator& i) const
            {
                return trie.in_range(i) && _prefix_check(i);
            }

        private:
            functor_t _prefix_check;
        };

        template <class F> //deduction guide
        PrefixedInRange(F) -> PrefixedInRange<F>;

        /** Ingredient for MixAlgorithmRangeAdapter - just alias for PrefixedInRange, but give more consistence when mixing iteration over
        * children range like: <ChildBegin, ChildInRange, SiblingNext>
        */
        template <class F>
        struct ChildInRange: PrefixedInRange<F>
        {
            using  PrefixedInRange<F>::PrefixedInRange;
        };

        template <class F> //deduction guide
        ChildInRange(F) -> ChildInRange<F>;

        /** Ingredient for MixAlgorithmRangeAdapter - implements `in_range` with extra predicate */
        template <class Predicate>
        struct TakeWhileInRange
        {
            /** \tparam Predicate take-while predicate, must be compatible with `bool(const iterator&)` */
            TakeWhileInRange(Predicate predicate) noexcept
                : _predicate(std::move(predicate))
            {}

            template <class Trie>
            bool _in_range(const Trie& trie, const typename Trie::iterator& i) const
            {
                return trie.in_range(i) && _predicate(i);
            }

        private:
            Predicate _predicate;
        };
        template <class F> //deduction guide
        TakeWhileInRange(F) -> TakeWhileInRange<F>;

        /** Ingredient for MixAlgorithmRangeAdapter - implements `find` to play role of begin */
        
        template <class AtomString>
        struct Find
        {
            Find(AtomString key)
                : _key(std::move(key))
            {}

            template <class Trie>
            auto _begin(const Trie& trie) const
            {
                return trie.find(_key);
            }

        private:
            const std::decay_t<AtomString> _key;
        };
        template <class AtomString> //deduction guide
        Find(AtomString) -> Find<AtomString>;

    }//ns:Ingredient

    /**
    Motivation: Trie and PrefixedRanges contains lot of algorithms to do conceptually similar actions in different way. 
        For example you can initialize iteration over begin, find, lower_bound... . But range-based c++ for loop allows 
        leverage only begin/end pair. 
        (Of course c++20 ranges library gives a way to overcome this but using runtime capabilities only).
    Rationale: range-base for loop expects from container 2 simple idioms "give me begin of the range and end of the range". 
        Programmers have several ways to implement such idiom:
            \li create proxy to feed begin/end in expected way (actually c++20 ranges do it) ;
            \li overrides some method by inheritance;
            \li provide lambda/callback to invoke correct functionality.
    Mixer gives another alternative by combining small implementations at compile time.
    \code
        Mixer<Trie, Ingredient::PrefixedBegin, Ingredient::SiblingNext>
    \endcode
    Allows specify that instead of `begin` used prefixed-begin and instead of `next` used sibling-next to create result 
    range
                
    */
    template <class Trie, class ... Mx>
    struct Mixer;

    /** Specialization of Mixer that provide default implementation */
    template <class Trie>
    struct Mixer<Trie> 
    {
        using iterator = typename Trie::iterator;
        explicit Mixer() = default;
        template<class SharedArguments>
        explicit Mixer(const SharedArguments& arguments) noexcept{}

        //default impl that invoke Trie::begin
        iterator _begin(const Trie& trie) const
        {
            return trie.begin();
        }
        
        //default impl that invoke Trie::next
        void _next(const Trie& trie, iterator& i) const
        {
            trie.next(i);
        }

        bool _in_range(const Trie& trie, const iterator& i) const
        {
            return trie.in_range(i);
        }

        iterator _end(const Trie& trie) const
        {
            return trie.end();
        }

        void _lower_bound(const Trie& trie, iterator& i, const typename Trie::key_t& k) const
        {
            i = trie.lower_bound(i, k);
        }

    };
    /** Specialization of Mixer that mixes multiple ingredients by apply move constructor */
    template <class Trie, class M, class ... Mx>
    struct Mixer<Trie, M, Mx...> : public M, public Mixer<Trie, Mx ... >
    {
        using base_t = Mixer<Trie, Mx ... >;

        explicit Mixer(M && m, Mx&& ... mx) noexcept
            : M (std::forward<M>(m))
            , base_t(std::forward<Mx>(mx)...)
        {}
        
        using base_begin_t = std::conditional_t< 
                                        details::has__begin<M, Trie>::value, M, base_t >;
        using base_begin_t::_begin;

        using base_next_t = typename std::conditional_t< 
                                        details::has__next<M, Trie>::value, M, base_t >;
        using base_next_t::_next;
        
        using base_in_range_t = typename std::conditional_t< 
                                        details::has__in_range<M, Trie>::value, M, base_t >;
        using base_in_range_t::_in_range;

        using base_end_t = typename std::conditional_t< 
                                        details::has__end<M, Trie>::value, M, base_t >;
        using base_end_t::_end;

        using base_lower_bound_t = typename std::conditional_t< 
                                        details::has__lower_bound<M, Trie>::value, M, base_t >;
        using base_lower_bound_t::_lower_bound;
    };
    namespace flur = OP::flur;

    /**
    *   flur library adapter for Trie.
    * Adapter implements ordered sequence (even more OrderedRangeOptimizedJoin) where element of sequence is
    *  Trie::iterator. 
    */
    template <class TTrie, class ... Mx>
    struct TrieSequence : public flur::OrderedSequenceOptimizedJoin< const typename TTrie::iterator& >
    {
        using trie_t = TTrie;
        using mixer_t = Mixer<trie_t, Mx ...>;
        using base_t = flur::OrderedSequenceOptimizedJoin< typename TTrie::iterator >;
        using mixer_iterator_t = typename trie_t::iterator;

        //using key_t = typename base_t::key_t;
        //using value_t = typename base_t::value_t;

        TrieSequence(std::shared_ptr<const trie_t> parent, mixer_t mixer = mixer_t{}) noexcept
            : _parent(std::move(parent))
            , _mixer(std::move(mixer))
        {
        }

        TrieSequence(std::piecewise_construct_t , std::shared_ptr<const trie_t> parent, Mx&& ... args) noexcept
            : _parent(std::move(parent))
            , _mixer(std::forward<Mx>(args)...)
        {
        }

        virtual void start() override
        {
            _pos = _mixer._begin(*_parent);
        }
       
        virtual bool in_range() const override
        {
            if(_pos.is_end())
                return false;
            return _mixer._in_range(*_parent, _pos );
        }

        virtual const mixer_iterator_t& current() const override
        {
            return _pos;
        }

        virtual void next() override
        {
            return _mixer._next(*_parent, _pos);
        }
        
        /** Note argument `key` not used as iterator, but key */
        virtual void lower_bound(const mixer_iterator_t& key) override
        {
            return _mixer._lower_bound(*_parent, _pos, key.key());
        }
        
        const std::shared_ptr<const trie_t>& get_parent() const
        {
            return _parent;
        }


    private:
        std::shared_ptr<const trie_t> _parent;
        mixer_t _mixer;
        mixer_iterator_t _pos;
    };

    template <class TTrie, class ... Mx>
    struct TrieSequenceFactory : OP::flur::FactoryBase
    {
        using sequence_t = TrieSequence<TTrie, Mx ...>;
        using trie_t = TTrie;
        using trie_ptr = std::shared_ptr <const TTrie>;
        using mixer_t = Mixer<trie_t, Mx ...>;

        constexpr TrieSequenceFactory(trie_ptr trie, Mx&& ... mx) noexcept
            :_trie(std::move(trie))
            , _mixer(std::forward<Mx>(mx)...) 
        {}
        
        constexpr auto compound() const& noexcept
        {
            return sequence_t(_trie, _mixer);
        }
        constexpr auto compound() && noexcept
        {
            return sequence_t(std::move(_trie), std::move(_mixer));
        }
    private:
        trie_ptr _trie;
        mixer_t _mixer;
    };

    template <class TTrie, class ... Mx>
    auto make_mixed_sequence_factory(std::shared_ptr<const TTrie> trie, Mx&& ...args) noexcept
    {
        TrieSequenceFactory<TTrie, Mx...> factory (std::move(trie), std::forward<Mx>(args)...);
        return OP::flur::make_lazy_range(
                std::move(factory)
        );
    }

    template <class TTrie, class ... Mx>
    auto make_mixed_sequence_factory(std::shared_ptr<TTrie> trie, Mx&& ...args) noexcept
    {
        return make_mixed_sequence_factory(std::const_pointer_cast<const TTrie>(trie), std::forward<Mx>(args)...);
    }

} //ns:OP::trie

#endif //_OP_TRIE_MIXEDADAPTER__H_
