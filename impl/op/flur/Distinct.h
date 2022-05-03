#pragma once
#ifndef _OP_FLUR_DISTINCT__H_
#define _OP_FLUR_DISTINCT__H_

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
    *   Make source ordered stream distinct
    */
    template <class EqFnc, class Src, class Base>
    struct Distinct : public Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        
        constexpr Distinct(Src&& src, EqFnc f) noexcept
            : _src(std::move(src))
            , _cmp(std::move(f))
        {
        }

        virtual void start()
        {
            if( !details::get_reference(_src).is_sequence_ordered() )
                throw std::runtime_error("unordered input sequence");
            details::get_reference(_src).start();
        }
        virtual bool in_range() const
        {
            return details::get_reference(_src).in_range();
        }
        virtual element_t current() const
        {
            return details::get_reference(_src).current();
        }
        virtual void next()
        {
            auto& rs = details::get_reference(_src);
            auto bypassed = rs.current();
            for(rs.next(); 
                rs.in_range() && _cmp(bypassed, rs.current());
                rs.next())
            {}
        }
    private:
        Src _src;
        EqFnc _cmp;
    };

    namespace details{ struct use_default_eq_t{}; }

    template <class EqFnc = details::use_default_eq_t>
    struct DistinctFactory : FactoryBase
    {

        template <class U = EqFnc>
        constexpr DistinctFactory(U f = EqFnc{}) noexcept
            : _cmp(std::move(f))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_conatiner_t = details::sequence_type_t<details::dereference_t<Src>>;
            using base_t = OrderedSequence<typename src_conatiner_t::element_t>;
            if constexpr(
                std::is_same_v<details::use_default_eq_t, EqFnc>)
            { 
                using cmp_t = std::equal_to<typename src_conatiner_t::element_t>;
                return Distinct<cmp_t, std::decay_t<Src>, base_t>(std::move(src), 
                    cmp_t{}
                    );
            }
            else
            {
                return Distinct<EqFnc, std::decay_t<Src>, base_t>(std::move(src), _cmp);
            }
        }
        EqFnc _cmp;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_DISTINCT__H_
