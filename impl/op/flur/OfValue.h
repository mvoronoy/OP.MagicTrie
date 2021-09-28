#pragma once
#ifndef _OP_FLUR_OFVALUE__H_
#define _OP_FLUR_OFVALUE__H_

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
    struct OfValue : public OrderedSequence<T>
    {
        constexpr OfValue(T src) noexcept
            :_src(std::move(src))
            , _retrieved(false)
        {
        }

        virtual void start()
        {
            _retrieved = false;
        }

        virtual bool in_range() const
        {
            return !_retrieved;
        }

        virtual T current() const
        {
            return details::get_reference(_src);
        }

        virtual void next()
        {
            _retrieved = true;
        }
    private:
        bool _retrieved;
        T _src;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFVALUE__H_
