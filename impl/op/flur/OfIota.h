#pragma once
#ifndef _OP_FLUR_OFIOTA__H_
#define _OP_FLUR_OFIOTA__H_

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
    /**
    *   Create conatiner of sequentially increasing values [begin, end).
    * Container is ordred on condition if for the boundary 
    * [begin, end) condition `(begin <= end)` is true. 
    */
    template <class T, class R = T>
    struct OfIota : public Sequence<R>
    {
        using this_t = OfIota<T, R>;
        using distance_t = std::ptrdiff_t;
        using bounds_t = std::tuple<T, T, distance_t>;
        constexpr OfIota(T&& begin, T&& end, distance_t step = 1) noexcept
            : _bounds(std::forward<T>(begin), std::forward<T>(end), step)
            , _current(std::get<1>(_bounds)) //end
        {
        }

        template <class AltBounds>
        constexpr OfIota(AltBounds&& bounds) noexcept
            : _bounds(std::forward<AltBounds>(bounds))
            , _current(std::get<1>(_bounds)) //end
        {}

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            if constexpr(OP::has_operators::less_v<T>)
            {
                // check begin <= end
                return (std::get<0>(_bounds) < std::get<1>(_bounds)) || 
                    (std::get<0>(_bounds) == std::get<1>(_bounds));
            }
            else 
                return false;
        }

        virtual void start() override
        {
            _current = std::get<0>(_bounds);
        }

        virtual bool in_range() const override
        {
            //if constexpr(OP::has_operators::less_v<T>)
            //{
            //    return (_current < std::get<1>(_bounds));
            //}
            //else
                return _current != std::get<1>(_bounds);
        }

        virtual R current() const override
        {
            return _current;
        }

        virtual void next() override
        {
            static_assert(
                OP::has_operators::plus_eq_v<T, distance_t> ||
                OP::has_operators::prefixed_plus_plus_v<T>, 
                "Type T must support `+=` or `++`T"
            );
            if constexpr(OP::has_operators::plus_eq_v<T, distance_t>)
            {
                _current += std::get<2>(_bounds);
            }
            else
            {
                //@! impl have no ability to advance in negative direction
                for(auto i = 0; in_range() && i < std::get<2>(_bounds); ++i)
                    ++_current;
            }
        }
    private:
        bounds_t _bounds;
        T _current;
    };


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFIOTA__H_
