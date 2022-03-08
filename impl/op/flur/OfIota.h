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
    * Container is ordred.
    */
    template <class T>
    struct OfIota : public OrderedSequence<T>
    {
        using this_t = OfIota<T>;
        using range_t = std::pair<T, T>;
        constexpr OfIota(T&& begin, T&& end) noexcept
            : _range(std::forward<T>(begin), std::forward<T>(end))
            , _current(_range.first)
        {
        }

        template <class AltPair>
        constexpr OfIota(AltPair&& pair) noexcept
            : _range(std::forward<AltPair>(pair))
            , _current(_range.first)
        {}

        virtual void start()
        {
            _current = _range.first;
        }

        virtual bool in_range() const
        {
            return _current != _range.second;
        }

        virtual T current() const
        {
            return _current;
        }

        virtual void next()
        {
            ++_current;
        }
        range_t _range;
        T _current;
    };


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFIOTA__H_
