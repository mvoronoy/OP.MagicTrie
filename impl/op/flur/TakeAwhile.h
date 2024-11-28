#pragma once
#ifndef _OP_FLUR_TAKEAWHILE__H_
#define _OP_FLUR_TAKEAWHILE__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Filter.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{

    template <class Fnc, class Src>
    struct TakeAwhile : public Sequence<details::sequence_element_type_t<Src>>
    {
        using element_t = details::sequence_element_type_t<Src>;

        template <class F>
        constexpr TakeAwhile(Src&& src, F f) noexcept
            :_src(std::move(src))
            , _predicate(std::move(f))
            , _end (true)
        {
        }

        virtual void start() override
        {
            auto& source = details::get_reference(_src);
            source.start();
            _end = false;
            seek();
        }

        virtual bool in_range() const override
        {
            return !_end;
        }

        virtual element_t current() const override
        {
            return details::get_reference(_src).current();
        }

        virtual void next() override
        {
            if(!_end)
            {
                details::get_reference(_src).next();
                seek();
            }
        }

    private:
        void seek()
        {
            auto& source = details::get_reference(_src);
            _end = !source.in_range() || !_predicate(source.current());
         }
        bool _end;
        Src _src;
        Fnc _predicate;
    };

    template <class Fnc>
    using TakeAwhileFactory = FilterFactoryBase<Fnc, TakeAwhile>;


} //ns:OP::flur

#endif //_OP_FLUR_TAKEAWHILE__H_
