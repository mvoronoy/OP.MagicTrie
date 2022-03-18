#pragma once
#ifndef _OP_FLUR_AGGREGATE__H_
#define _OP_FLUR_AGGREGATE__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/ftraits.h>

namespace OP::flur
{
    template <class Src, class Element, class FAgg, class FComplete>
    struct Aggregate : public Sequence<Element>
    {
        using base_t = Sequence<Element>;
        using element_t = typename base_t::element_t;
        
        constexpr Aggregate(Src&& src, FAgg&& agg, FComplete&& complete) noexcept
            : _src(std::move(src))
            , _agg(std::forward<FAgg>(agg))
            , _complete(std::forward<FComplete>(complete))
        {
        }

        virtual void start()
        {
            details::get_reference(_src).start();
        }
        virtual bool in_range() const
        {
            return details::get_reference(_src).in_range();
        }
        virtual element_t current() const
        {
            return _applicator(details::get_reference(_src).current());
        }
        virtual void next()
        {
            details::get_reference(_src).next();
        }
    private:
        void aggregate()
        {
        }
        element_t _current;
        Src _src;
        FAgg _agg;
        FComplete _complete;
    };

} //ns: OP::flur
#endif //_OP_FLUR_AGGREGATE__H_
