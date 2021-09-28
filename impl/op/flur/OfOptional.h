#pragma once
#ifndef _OP_FLUR_OFOPTIONAL__H_
#define _OP_FLUR_OFOPTIONAL__H_

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
    * Treate std::optional as container of 0 or 1 element of type T.
    * Note container of 1 item is traeted as sorted
    */
    template <class T>
    struct OfOptional : public OrderedSequence<T>
    {
        using container_t = std::optional<T>;
        constexpr OfOptional(container_t src) noexcept
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
            return !_retrieved && _src.has_value();
        }

        virtual T current() const
        {
            return *_src;
        }

        virtual void next()
        {
            _retrieved = true;
        }
    private:
        bool _retrieved;
        container_t _src;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFOPTIONAL__H_
