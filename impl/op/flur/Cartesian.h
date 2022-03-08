#pragma once
#ifndef _OP_FLUR_CARTESIAN__H_
#define _OP_FLUR_CARTESIAN__H_

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

    template <class Src, class Alien, class Fnc, class R> 
    struct Cartesian : public Sequence< R >
    {
        using result_t = R;

        template <class F>
        constexpr Cartesian(Src&& src, Alien&& alien, F f) noexcept
            :_src(std::move(src))
            , _alien(std::move(alien))
            , _applicator(std::move(f))
        {
        }
        virtual void start()
        {
            details::get_reference(_src).start();
            details::get_reference(_alien).start();
        }
        virtual bool in_range() const
        {
            return details::get_reference(_src).in_range() 
                && details::get_reference(_alien).in_range();
        }
        virtual result_t current() const
        {
            return _applicator(
                details::get_reference(_src).current(), details::get_reference(_alien).current());
        }
        virtual void next()
        {
            details::get_reference(_alien).next();
            if (!details::get_reference(_alien).in_range())
            {
                details::get_reference(_src).next();
                if (details::get_reference(_src).in_range())
                    details::get_reference(_alien).start();
            }
        }
        Src _src;
        Alien _alien;
        //std::function<R(const typename Src::element_t&, const typename Alien::element_t&)> _applicator;
        Fnc _applicator;
    };


    template <class Alien, class F >
    struct CartesianFactory : FactoryBase
    {
        using pure_f = std::decay_t<F>;
        using alien_src_t = details::sequence_type_t<Alien>;

        //using second_src_t = std::conditional_t < std::is_base_of_v<FactoryBase, Alien>,
        //    std::decay_t <decltype(std::declval<Alien>().compound())>,
        //    Alien>;
        constexpr CartesianFactory(Alien&& alien, F f) noexcept
            :_alien(std::move(alien))
            , _applicator(std::move(f))
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using result_t = decltype(
                _applicator(details::get_reference(src).current(), 
                    details::get_reference(std::declval<const alien_src_t&>()).current())
                );
            //using result_t = typename f_traits_t::result_t;
            return Cartesian<Src, alien_src_t, F, result_t>(
                std::move(src),
                std::move(details::get_reference(_alien).compound()),
                _applicator);
        }

        Alien _alien;
        F _applicator;
    };

    
} //ns:flur
} //ns:OP

#endif //_OP_FLUR_CARTESIAN__H_
