#pragma once
#ifndef _OP_FLUR_OFITERATORS__H_
#define _OP_FLUR_OFITERATORS__H_

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
    *   Create conatiner by pair of iterators.
    * Container is non-ordred.
    */
    template <class T>
    struct OfIterators : public Sequence<decltype(*std::declval<T>())>
    {
        using element_t = typename OfIterators<T>::element_t;
        using this_t = OfIterators<T>;
        constexpr OfIterators(T begin, T end) noexcept
            : _begin(std::move(begin))
            , _end(std::move(end))
            , _current(_end)
        {
        }
        explicit constexpr OfIterators(std::pair<T, T> pair) noexcept
            : this_t(std::move(pair.first), std::move(pair.second))
        {}

        virtual void start()
        {
            _current = _begin;
        }

        virtual bool in_range() const
        {
            return _current != _end;
        }

        virtual element_t current() const
        {
            return _current;
        }

        virtual void next()
        {
            ++_current;
        }

        T _begin, _end, _current;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFITERATORS__H_
