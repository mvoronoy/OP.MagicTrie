#pragma once
#ifndef _OP_FLUR_FILTER__H_
#define _OP_FLUR_FILTER__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{

    template <class Fnc, class Src, class Base>
    struct Filter : public Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;

        template <class F>
        constexpr Filter(Src&& src, F f) noexcept
            : _src(std::move(src))
            , _predicate(std::move(f))
            , _end (true)
        {
        }

        
        bool is_sequence_ordered() const noexcept override
        {
            return details::get_reference(_src).is_sequence_ordered();
        }

        virtual void start() override
        {
            auto& source = details::get_reference(_src);
            source.start();
            _end = !source.in_range();
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
            details::get_reference(_src).next();
            seek();
        }

    private:
        void seek()
        {
            auto& source = details::get_reference(_src);
            for (; !_end && !(_end = !source.in_range()); source.next())
            {
                if (_predicate(source.current()))
                    return;
            }
        }

        bool _end;
        Src _src;
        Fnc _predicate;
    };

    template <class Fnc, template <typename...> class Impl>
    struct FilterFactoryTrait
    {
        using functor_t = Fnc;

        template <class Src>
        using src_conatiner_t = details::sequence_type_t<details::dereference_t<Src>>;

        template <class Src>
        using sequence_base_t = std::conditional_t<
            src_conatiner_t<Src>::ordered_c,
            OrderedSequence<typename src_conatiner_t<Src>::element_t>,
            Sequence<typename src_conatiner_t<Src>::element_t>
        >;

        template <class Src>
        using result_sequence_t = Impl<Fnc, Src, sequence_base_t<Src> >;
    };


    template <class FTrait>
    struct FilterFactoryBase : FactoryBase
    {
        using holder_t = typename FTrait::functor_t;//std::function<Fnc>;

        template <class U>
        constexpr FilterFactoryBase(U f) noexcept
            : _fnc(std::move(f))
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            using result_t = typename FTrait::template result_sequence_t<Src>;
            return result_t(std::forward<Src>(src), _fnc);
        }
        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            using result_t = typename FTrait::template result_sequence_t<Src>;
            return result_t(std::forward<Src>(src), std::move(_fnc));
        }
        holder_t _fnc;
    };
    template <class Fnc>
    using FilterFactory = FilterFactoryBase< FilterFactoryTrait<Fnc, Filter> >;

}  //ns:OP::flur


#endif //_OP_FLUR_FILTER__H_
