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
    *   Create conatiner of strictly 1 item.
    * Container is ordred.
    */
    template <class T>
    struct OfIota : public OrderedSequence<T>
    {
        using this_t = OfIota<T>;
        constexpr OfIota(T begin, T end) noexcept
            : _begin(std::move(begin))
            , _end(std::move(end))
            , _current(_end)
        {
        }
        constexpr OfIota(std::pair<T, T> pair) noexcept
            : this_t(pair.first, pair.second)
        {}

        virtual void start()
        {
            _current = _begin;
        }

        virtual bool in_range() const
        {
            return _current != _end;
        }

        virtual T current() const
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

#endif //_OP_FLUR_OFIOTA__H_
