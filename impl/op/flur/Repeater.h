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

    /**
    * Repeater is optimization part that may be used for cases when source is slow, but 
    *   multiple runs of `start` are expected. Repeater stores full scope of data on first
    *   run then allows to repeat without involvement of source sequence.
    *
    * \tparam Base - ordered or unordered sequence (discovered automatically depending on source)
    * \tparam Src - source sequnce to store and then repeat
    * \tparam Container - storage for elements std::vector by default
    */
    template <class Base, class Src, class Container = std::deque<std::decay_t<typename Base::element_t> >>
    class Repeater : public Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using this_t = Repeater<Base, Container>;
        using iterator_t = typename Container::const_iterator;
        Src _src;
        Container _container;
        size_t _latch = 0;
        size_t _current = -1;
        void peek()
        {
            if (OP::flur::details::get_reference(_src).in_range())
                _container.emplace_back(
                    std::move(
                        OP::flur::details::get_reference(_src).current()
                    )
                );
        }
    public:
        constexpr Repeater(Src&& src)
            :_src(std::move(src))
        {}
        virtual void start()
        {
            if (++_latch == 1)
            {
                OP::flur::details::get_reference(_src).start();
                peek();
            }
            _current = 0;
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
            if (_latch == 1)
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
    struct RepeaterFactory : FactoryBase
    {
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using target_container_t = std::decay_t<decltype(OP::flur::details::get_reference(src))>;
            using src_container_t = OP::flur::details::unpack_t<Src>;
            using element_t = typename target_container_t::element_t;
            using element_ref = const element_t&;
            using base_t = std::conditional_t< (target_container_t::ordered_c),
                OrderedSequence<element_ref>,
                Sequence<element_ref>
            >;
            return Repeater<base_t, Src>(std::move(src));
        }

    };
} //ns:flur
} //ns:OP

#endif //_OP_FLUR_FILTER__H_
