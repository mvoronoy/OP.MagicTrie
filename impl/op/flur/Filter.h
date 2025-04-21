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

    template <class Fnc, class Src>
    struct Filter : public Sequence<details::sequence_element_type_t<Src>>
    {
        using element_t = details::sequence_element_type_t<Src>;

        template <class F>
        constexpr Filter(Src&& src, F f) noexcept
            : _src(std::move(src))
            , _predicate(std::move(f))
            , _end(true)
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
            _seq_state.start();
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
            _seq_state.next();
            seek();
        }

    private:
        void seek()
        {
            auto& source = details::get_reference(_src);
            for (; !_end && !(_end = !source.in_range()); source.next())
            {
                using namespace OP::currying;
                if (recomb_call(_predicate, source.current(), _seq_state))
                    return;
            }
        }

        bool _end;
        Src _src;
        Fnc _predicate;
        SequenceState _seq_state{};
    };

    template <class Fnc, template <typename...> class Impl>
    struct FilterFactoryTrait
    {
        using functor_t = Fnc;

        template <class Src>
        using src_conatiner_t = details::sequence_type_t<details::dereference_t<Src>>;

        template <class Src>
        using result_sequence_t = Impl<Fnc, Src>;
    };


    template <class F, template <typename...> class Impl>
    struct FilterFactoryBase : FactoryBase
    {
        template <class Src>
        using result_sequence_t = Impl<F, Src>;

        template <class FLike>
        explicit constexpr FilterFactoryBase(FLike&& f) noexcept
            : _fnc(std::forward<FLike>(f))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            return result_sequence_t<Src>(std::move(src), _fnc);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            return result_sequence_t<Src>(std::move(src), std::move(_fnc));
        }

    private:
        F _fnc;
    };

    template <class F>
    using FilterFactory = FilterFactoryBase<F, Filter>;

}  //ns:OP::flur


#endif //_OP_FLUR_FILTER__H_
