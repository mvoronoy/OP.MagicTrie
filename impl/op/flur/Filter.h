#pragma once
#ifndef _OP_FLUR_FILTER__H_
#define _OP_FLUR_FILTER__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{

    template <class Fnc, class Src, class Base>
    struct Filter : public Base
    {
        using src_container_t = details::unpack_t<Src>;
        using element_t = typename src_container_t::element_t;

        template <class F>
        constexpr Filter(Src&& src, F&& f) noexcept
            :_src(std::move(src))
            , _predicate(std::move(f))
            , _end (true)
        {
            
        }
        virtual void start()
        {
            for (_src.start(); !(_end = !_src.in_range()); _src.next())
            {
                if (_predicate(_src.current()))
                    return;
            }
        }
        virtual bool in_range() const
        {
            return !_end;
        }
        virtual element_t current() const
        {
            return _src.current();
        }
        virtual void next()
        {
            for (_src.next(); !_end && !(_end = !_src.in_range()); _src.next())
            {
                if (_predicate(_src.current()))
                    return;
            }
        }

        bool _end;
        Src _src;
        Fnc _predicate;
    };


    template <class Fnc>
    struct FilterFactory : FactoryBase
    {
        using holder_t = std::function<Fnc>;

        template <class U>
        constexpr FilterFactory(U f) noexcept
            : _fnc(std::move(f))
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_conatiner_t = details::unpack_t<Src>;
            if constexpr (src_conatiner_t::ordered_c)
            {
                using filter_base_t = OrderedSequence<typename src_conatiner_t::element_t>;
                return Filter<Fnc, src_conatiner_t, filter_base_t>(details::unpack(std::move(src)), _fnc);
            }
            else
            {
                using filter_base_t = Sequence<typename src_conatiner_t::element_t>;
                return Filter<Fnc, src_conatiner_t, filter_base_t>(details::unpack(std::move(src)), _fnc);
            }
        }
        Fnc _fnc;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_FILTER__H_
