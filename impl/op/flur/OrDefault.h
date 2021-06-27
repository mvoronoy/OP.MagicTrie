#pragma once
#ifndef _OP_FLUR_ORDEFAULT__H_
#define _OP_FLUR_ORDEFAULT__H_

#include <functional>
#include <memory>

#include <op/common/Utils.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{

    /** Sequence that implements logic to take information from source, but if source empty
    then consume from alternative source
    \tparam Base - base for implementation (may be Sequence or OrderedSequence)
    \tparam Src - source for pipeline
    \tparam Alt - alternative source to consume if Src is empty
    */
    template <class Base, class Src, class Alt>
    struct OrDefault : Base
    {
        static_assert(std::is_convertible_v<typename Src::element_t, typename Alt::element_t>,
            "Alternative source must produce compatible values for 'OrDefault'");

        using base_t = Base;
        using element_t = typename base_t::element_t;

        constexpr OrDefault(Src&& src, Alt&& alt) noexcept
            : _src(std::move(src))
            , _alt(std::move(alt))
            , _use_alt(false)
        {
        }

        virtual void start()
        {
            _use_alt = false;
            _src.start();
            if (!_src.in_range())
            {
                _use_alt = true;
                _alt.start();
            }
        }

        virtual bool in_range() const
        {
            return _use_alt ? _alt.in_range() : _src.in_range();
        }
        virtual element_t current() const
        {
            return _use_alt ? _alt.current() : _src.current();
        }
        virtual void next()
        {
            _use_alt ? _alt.next() : _src.next();
        }

        bool _use_alt;
        Src _src;
        Alt _alt;
    };

    /** Factory to create OrDefault*/
    template <class Alt>
    struct OrDefaultFactory : FactoryBase
    {
        using alt_holder_t = details::unpack_t<Alt>;

        constexpr OrDefaultFactory(Alt alt) noexcept
            : _alt(std::move(alt))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_container_t = details::unpack_t<Src>;
            
            if constexpr (OP::utils::is_generic<Alt, Sequence>::value)
            { // value is some container
                using base_t = std::conditional_t< (src_container_t::ordered_c && alt_holder_t::ordered_c),
                    OrderedSequence<typename src_container_t::element_t>,
                    Sequence<typename src_container_t::element_t>
                >;
                return
                    OrDefault<base_t, src_container_t, alt_holder_t>(
                        details::unpack(std::move(src)), 
                        _alt);
            }
            else if constexpr (std::is_base_of_v < FactoryBase, Alt> )
            { // provided parameter is some factory
                using base_t = std::conditional_t< (src_container_t::ordered_c&& alt_holder_t::ordered_c),
                    OrderedSequence<typename src_container_t::element_t>,
                    Sequence<typename src_container_t::element_t>
                >;
                return
                    OrDefault<base_t, src_container_t, alt_holder_t>(
                        details::unpack(std::move(src)),
                        details::unpack(_alt));
            }
            else //specified parameter just a plain value
            {
                //1 value is already ordered
                using base_t = std::conditional_t< src_container_t::ordered_c,
                    OrderedSequence<typename src_container_t::element_t>,
                    Sequence<typename src_container_t::element_t>
                >;
                // just a value, treate this as plain value
                return OrDefault<base_t, src_container_t, OfValue< Alt >>(
                    std::move(details::unpack(std::move(src))), 
                    OfValue< Alt >(_alt)
                    );
            }
        }
    private:
        Alt _alt;
    };


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_ORDEFAULT__H_
