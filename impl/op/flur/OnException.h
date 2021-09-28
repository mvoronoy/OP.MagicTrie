#pragma once
#ifndef _OP_FLUR_ONEXCEPTION__H_
#define _OP_FLUR_ONEXCEPTION__H_

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
    //template <class Src, class Dest>
    //struct ReconcileReferenceStreamValue
    //{
    //    static_assert(
    //        std::is_convertible_v<Src, Dest>,
    //        "A must be compatible with B by producing type"
    //        );
    //    constexpr bool need_correction_c =
    //        (std::is_lvalue_reference_v<Src> && !std::is_lvalue_reference_v<Dest>)
    //        || (!std::is_lvalue_reference_v<Src> && std::is_lvalue_reference_v<Dest>)
    //        ;

    //    using holder_t = std::decay_t<A>;

    //};

    /** 
    *   OnException allows intercept exception from previous pipeline and to produce 
    *   some payload instead. 
    *   Important design concept about exception interception that it works only on 
    *   part of pipeline preceeding OnException declaration. For example:
    *   \code
    *       using namespace OP::flur;
    *       size_t invocked = 0;
    *       auto pipeline = src::generator([&]() {
    *           ++invocked;
    *           return invocked > 0
    *               ? throw std::logic_error("biger than 0")
    *               : std::optional<int>{invocked};
    *       }))
    *       >> then::on_exception<std::logic_error>(src::of_value(42)) //intercept only std::logic_error, but not std::runtime_error
    *       >> then::mapping([](auto n) ->int {throw std::runtime_error("re-raise error"); })
    *       >> then::on_exception<std::runtime_error>(src::of_value(73))
    *       ;
    *   \endcode
    *   In this code generator raises exception, then instead of exception 42 is resolved 
    *   after we try to map result but also raise exception. Final exception interceptor
    *   convert it to 73. So result pipeline produces [73, 73]
    *
    * \tparam Src - source pipeline
    * \tparam AltFactory - some source to use instead of Src in case of exception. 
    *                      This source must be compatible with Src by type (std::is_convertible)
    */
    template <class Base, class Src, class Alt, class Ex>
    struct OnException : public Base
    {
        using base_t = Base;
        using alt_source_t = Alt;
        using source_t = Src;
        using element_t = typename base_t::element_t;

        constexpr OnException(Src&& src, Alt alt) noexcept
            :_source{std::move(src)}
            , _alt(std::move(alt))
            , _use_alt(false)
        {
            
        }
        virtual void start()
        {
            //if already consuming from alt - just reset back to origin src
            _use_alt = false;
            try
            {
                _source.start();
            } catch(const Ex& )
            {
                handle_exception();
            }
        }
        virtual bool in_range() const
        {
            if (!_use_alt)
            {
                try
                {
                    return _source.in_range();
                }
                catch (const Ex&)
                {
                    handle_exception();
                }
            }
            return _alt.in_range();
        }
        virtual element_t current() const
        {
            if (!_use_alt)
            {
                try
                {
                    return _source.current();
                }
                catch (const Ex&)
                {
                    handle_exception();
                }
            }
            return _alt.current();
        }
        virtual void next()
        {
            if (_use_alt)
            {
                _alt.next();
            }
            else
            {
                try
                {
                    _source.next();
                }
                catch (const Ex&)
                {
                    handle_exception();
                    //no call of _alt.next() since handle_exception just invoked start()
                }
            }
        }
    private:
        void handle_exception() const
        {
            _alt.start();
            _use_alt = true;
        }
        source_t _source;
        mutable alt_source_t _alt;
        mutable bool _use_alt;
    };


    template <class Alt, class Ex>
    struct OnExceptionFactory : FactoryBase
    {

        constexpr OnExceptionFactory(Alt&& alt) noexcept
            : _alt_factory(std::move(alt))
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_conatiner_t = details::unpack_t<Src>;
            using src_element_t = typename src_conatiner_t::element_t;
            using alt_container = details::unpack_t<Alt>;
            using alt_element_t = typename alt_container::element_t;
            static_assert(
                std::is_convertible_v<typename alt_container::element_t, typename src_conatiner_t::element_t>,
                "Alt must be compatible with Src by producing type"
            );
            constexpr bool need_correction_c =
                (std::is_lvalue_reference_v<src_element_t> && !std::is_lvalue_reference_v<alt_element_t>)
                || (!std::is_lvalue_reference_v<src_element_t> && std::is_lvalue_reference_v<alt_element_t>)
                ;
            // Element type can be reference only if both are reference, otherwise use decay
            using element_t = std::conditional_t< need_correction_c, std::decay_t<src_element_t>, src_element_t>;

            using base_t = std::conditional_t<
                (src_conatiner_t::ordered_c&& alt_container::ordered_c),
                OrderedSequence<element_t >,
                Sequence<element_t>
            >;

            return OnException<base_t, src_conatiner_t, alt_container, Ex>(
                    details::unpack(std::move(src)), details::unpack(_alt_factory)
                    );
        }
        Alt _alt_factory;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_ONEXCEPTION__H_
