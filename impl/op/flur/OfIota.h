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
        struct Bounds
        {
            T _begin;
            T _end;
            distance_t _step;
        };
        using bounds_t = Bounds;
        constexpr OfIota(T&& begin, T&& end, distance_t step = 1) noexcept
            : _bounds{ std::forward<T>(begin), std::forward<T>(end), step }
            , _current(_bounds._end) //end
        {
        }

        explicit constexpr OfIota(bounds_t bounds) noexcept
            : _bounds(std::move(bounds))
            , _current(_bounds._end) //end
        {}

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            if constexpr(OP::has_operators::less_v<T>)
            {
                // check begin <= end
                return (_bounds._begin < _bounds._end) || 
                    (_bounds._begin == _bounds._end);
            }
            else 
                return false;
        }

        virtual void start() override
        {
            _current = _bounds._begin;
        }

        virtual bool in_range() const override
        {
            return _current != _bounds._end;
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
                _current += _bounds._step;
            }
            else
            {
                //@! impl have no ability to advance in negative direction
                for(auto i = 0; in_range() && i < _bounds._step; ++i)
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
