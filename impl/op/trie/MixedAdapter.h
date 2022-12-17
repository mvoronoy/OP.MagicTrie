#pragma once
#ifndef _OP_TRIE_MIXEDADAPTER__H_
#define _OP_TRIE_MIXEDADAPTER__H_
#include <op/flur/flur.h>

#include <memory>
#include <functional>
#include <type_traits>
#include <op/common/has_member_def.h>


namespace OP::trie
{
    /**Implement functor for subrange method to implement predicate that detects end of range iteration*/
    struct StartWithPredicate
    {
        StartWithPredicate(atom_string_t prefix) noexcept
            : _prefix(std::move(prefix))
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
        const atom_string_t _prefix;
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
        /** Declare SFINAE predicate to test if class  has member lower_bound */
        OP_DECLARE_CLASS_HAS_MEMBER( _lower_bound )
    }
    

    template <class Trie>
    struct Ingredient
    {
        using iterator = typename Trie::iterator;
        /** Ingredient for MixAlgorithmRangeAdapter - allows range use `first_child` method instead of `begin` */
        struct ChildBegin
        {
            ChildBegin(iterator iter)
                : _iterator(std::move(iter))
            {
            }

            iterator _begin(const Trie& trie) const
            {
                return trie.first_child(_iterator);
            }
        private:
            /** Mutable since Trie can update version of iterartor */
            mutable iterator _iterator;
        };
        /** Ingredient for MixAlgorithmRangeAdapter - allows range use `find(key | `first_child` method instead of `begin` */
        struct ChildOfKeyBegin
        {
            
            ChildOfKeyBegin(atom_string_t key)
                : _key(std::move(key))
            {
            }

            iterator _begin(const Trie& trie) const
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
            void _next(const Trie& trie, iterator& i) const
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
            iterator _begin(const Trie& trie) const
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
            OP_CONSTEXPR_CPP20 PrefixedLowerBound(atom_string_t prefix) noexcept
                : _prefix(std::move(prefix))
            {
            }
            /** 
            * \param i - [out] result iterator
            */
            void _lower_bound(const Trie& trie, iterator& i, const atom_string_t& key) const
            {
                auto kb = std::begin(key);
                auto ke = std::end(key);
                auto pb = std::begin(_prefix);
                auto pe = std::end(_prefix);
                //do lexicographic compare to check if ever key starts with prefix 
                int cmpres = OP::ranges::str_lexico_comparator(pb, pe, kb, ke);
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
            const atom_string_t _prefix;
        };


        /** Ingredient for MixAlgorithmRangeAdapter - allows range customize `in_range` in a way to check that 
        * range iterates over items with specific prefix */
        struct PrefixedInRange
        {
            template <class F>
            PrefixedInRange(F pred)
                : _prefix_check(std::move(pred))
                {}

            bool _in_range(const Trie& trie, const iterator& i) const
            {
                return trie.in_range(i) && _prefix_check(i);
            }
        private:
            const std::function<bool(const iterator&)> _prefix_check;
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
                : _predicate(std::get<bool(const iterator&)>(args)) 
            {}

            /** \tparam Predicate take-while predicate, must be compatible with `bool(const iterator&)` */
            template <class Predicate>
            TakeWhileInRange(Predicate predicate) noexcept
                : _predicate(std::move(predicate))
            {}

            bool _in_range(const Trie& trie, const iterator& i) const
            {
                return trie.in_range(i) && _predicate(i);
            }

        private:
            const std::function< bool(const iterator&)> _predicate;
        };

        /** Ingredient for MixAlgorithmRangeAdapter - implments `find` to play role of begin */
        struct Find
        {
            Find(atom_string_t key)
                : _key(std::move(key))
            {}

            typename Trie::iterator _begin(const Trie& trie) const
            {
                return trie.find(_key);
            }

        private:
            const atom_string_t _key;
        };

    };

    /**
    Motivation: Trie and PrefixedRanges contains lot of algorithms to do conceptually similar actions in different way. 
        For example you can initialize iteration over begin, find, lower_bound... . But range-based c++ for loop allows 
        leverage only begin/end pair. 
        (Of course c++20 ranges library gives a way to overcome this but using runtime capabilities only).
    Rationale: range-base for loop expects from container 2 simple idioms "give me begin of range and end of range". 
        Programmers have several ways to implement such idioma:
            \li create proxy to feed begin/end in expected way (actually c++20 ranges do it) ;
            \li overrides some method by inheritance;
            \li provide lambda/callback to invoke correct functionality.
    Mixer gives another alternative by combining small implmentations at compile time.
    \code
        Mixer<Trie, Ingedient::PrefixedBegin, Ingedient::SiblingNext>
    \endcode
    Allows specify that instead of `begin` used prefixed-begin and instead of `next` used sibling-next to create result 
    range
                
    */
    template <class Trie, class ... Mx>
    struct Mixer;

    /** Specialization of Mixer that provide default implmentation */
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

        /*iterator _lower_bound(const Trie& trie, const atom_string_t& key) const
        {
            return trie.lower_bound(key);
        } */

        void _lower_bound(const Trie& trie, iterator& i, const atom_string_t& k) const
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
        
        using base_begin_t = std::conditional_t< OP_CHECK_CLASS_HAS_MEMBER(M, _begin), M, base_t >;
        using base_begin_t::_begin;

        using base_next_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _next), M, base_t >::type;
        using base_next_t::_next;
        
        using base_in_range_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _in_range), M, base_t >::type;
        using base_in_range_t::_in_range;

        using base_end_t = typename std::conditional< OP_CHECK_CLASS_HAS_MEMBER(M, _end), M, base_t >::type;
        using base_end_t::_end;

        using base_lower_bound_t = typename std::conditional< 
            OP_CHECK_CLASS_HAS_MEMBER(M, _lower_bound), M, base_t >::type;
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
    auto make_mixed_sequence_factory (std::shared_ptr<const TTrie> trie, Mx&& ...args)
    {
        TrieSequenceFactory<TTrie, Mx...> factory (std::move(trie), std::forward<Mx>(args)...);
        return OP::flur::make_lazy_range(
                std::move(factory)
        );
    }
/*        
    template <class TTrie, class ... Mx>
    std::shared_ptr< MixAlgorithmRangeAdapter<TTrie, Mx...> const>
    make_mixed_range(std::shared_ptr<const TTrie> trie, Mx&& ...args)
    {
        using mixed_t = MixAlgorithmRangeAdapter<TTrie, Mx...>;
        return std::make_shared<mixed_t>(std::piecewise_construct, std::move(trie), std::forward<Mx>(args)...);
    }
    template <class TTrie, class ... Mx>
    std::shared_ptr< MixAlgorithmRangeAdapter<TTrie, Mx...> const>
    make_mixed_range(std::shared_ptr<TTrie> trie, Mx&& ...args)
    {
        return make_mixed_range(std::const_pointer_cast<TTrie const>(trie), std::forward<Mx>(args)...);
    }

*/
} //ns:OP::trie

#endif //_OP_TRIE_MIXEDADAPTER__H_
