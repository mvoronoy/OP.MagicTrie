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
        template < class U = T >
        constexpr OfValue(U&& src)
            :_src(std::forward<U>(src))
            , _retrieved(false)
        {
        }

        //constexpr OfValue(const T& src)
        //    :_src(src)
        //    , _retrieved(false)
        //{
        //}
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
            return /*details::get_reference*/(_src);
        }

        virtual void next()
        {
            _retrieved = true;
        }
    private:
        bool _retrieved = false;
        T _src;
    };
    /**
    *   Create conatiner of strictly 1 item that evaluates each time when iteration starts.
    * Container is ordred.
    */
    template <class T, class F>
    struct OfLazyValue : public OrderedSequence<T>
    {
        using gen_t = F;
        constexpr OfLazyValue(gen_t gen) noexcept
            : _gen(std::move(gen))
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
            return _gen();
        }

        virtual void next()
        {
            _retrieved = true;
        }
    private:
        bool _retrieved;
        gen_t _gen;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFVALUE__H_
