#pragma once
#ifndef _OP_FLUR_REPEATER__H_
#define _OP_FLUR_FILTER__H_

#include <functional>
#include <memory>
#include <optional>
#include <deque>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    namespace details
    {
        template <class R, class TContainer>
        void post_element(TContainer& container, size_t, R&& r)
        {
            container.emplace_back(std::move(r));
        }

        template <class R, class T, size_t N>
        void post_element(std::array<T, N>& container, size_t n, R&& r)
        {
            container.at(n) = std::move(r);
        }
    }
    /**
    * Repeater is sequence that uses iteration over source only once, all another iteration 
    * are taken from internal container.
    * That may be used for cases when source is slow, but 
    *   multiple runs of `start` are expected. Repeater stores full scope of data on first
    *   run then allows to repeat without involvement of source sequence.
    *
    * \tparam Base - ordered or unordered sequence (discovered automatically depending on source)
    * \tparam Src - source sequnce to store and then repeat
    * \tparam Container - storage for elements ( like `std::vector` or `std::deque`)
    */
    template <class Base, class Src, class Container >
    class Repeater : public Base
    {
        using base_t = Base;
        using iterator_t = typename Container::const_iterator;
        Src _src;
        Container _container;
        size_t _generation = 0;
        size_t _current = -1;
        void peek()
        {
            auto& rsrc = OP::flur::details::get_reference(_src);
            if (rsrc.in_range())
               details::post_element( 
                   _container, _current, std::move(rsrc.current())
                );
        }
    public:
        using element_t = typename base_t::element_t;
        using this_t = Repeater<Base, Src, Container>;

        constexpr Repeater(Src&& src) noexcept
            : _src(std::move(src))
        {}

        /** Repeater is ordered on condition if source sequence is ordered */
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            return _src.is_sequence_ordered();
        }

        virtual void start()
        {
            _current = 0;
            if (++_generation == 1)
            {
                OP::flur::details::get_reference(_src).start();
                peek();
            }
        }

        virtual bool in_range() const
        {
            return _current < _container.size();
        }
        
        virtual element_t current() const
        {
            return _container[_current];
        }
        
        virtual void next()
        {
            if (_generation == 1)
            {
                OP::flur::details::get_reference(_src).next();
                peek();
            }
            ++_current;
        }
    };

    /**
    * Factory for Repeater class
    */
    template <template <typename...> class TContainer = std::deque>
    struct RepeaterFactory : FactoryBase
    {
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_sequence_t = details::dereference_t<Src>;
            using src_element_t = typename src_sequence_t::element_t;
            using element_t = std::decay_t<src_element_t>;
            using element_ref = const element_t&;
            using base_t  = Sequence<element_ref>;
            using storage_t = TContainer<element_t>;

            return Repeater<base_t, Src, storage_t>(std::move(src));
        }

    };
} //ns:flur
} //ns:OP

#endif //_OP_FLUR_FILTER__H_
