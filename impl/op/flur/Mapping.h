#pragma once
#ifndef _OP_FLUR_MAPPING__H_
#define _OP_FLUR_MAPPING__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{

    template <class Src, class R, class F>
    struct Mapping : public Sequence<R>
    {
        template <class U>
        constexpr Mapping(Src&& src, U f) noexcept
            : _src(std::move(src))
            , _applicator(f)
        {
        }
        virtual void start()
        {
            _src.start();
        }
        virtual bool in_range() const
        {
            return _src.in_range();
        }
        virtual R current() const
        {
            return _applicator(_src.current());
        }
        virtual void next()
        {
            _src.next();
        }

        Src _src;
        using element_t = typename Src::element_t;
        F _applicator;
    };

    template < class F >
    struct MappingFactory : FactoryBase
    {
        constexpr MappingFactory(F f) noexcept
            : _applicator(f)
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using target_container_t = std::decay_t<details::unpack_t<Src>>;
            using result_t = decltype(_applicator(std::declval<target_container_t>().current()));

            return Mapping<target_container_t, result_t, F>(
                std::move(details::unpack(std::move(src))), 
                _applicator);
        }
        F _applicator;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_MAPPING__H_
