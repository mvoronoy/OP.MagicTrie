#pragma once
#ifndef _OP_FLUR_CONDITIONAL__H_
#define _OP_FLUR_CONDITIONAL__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Proxy.h>

namespace OP::flur
{
    namespace details
    {
        template <class T>
        struct eval_condition
        {
            T _t;
            constexpr eval_condition(T t) noexcept
                : _t(std::move(t))
            {}
            
            constexpr bool operator()() const noexcept
            {
                return _t();
            }
        };

        template <>
        struct eval_condition<bool>
        {
            bool _v;
             constexpr eval_condition(bool v) noexcept
                 :_v(v)
             {
             }
            
            constexpr bool operator()() const noexcept
            {
                return _v;
            }
        };
    };

    /**
    * \brief allows conditionaly select between two options how to construc sequnce.
    *
    * Provides factory that conditionally delegates construction of sequence to another
    * factories. Condition is checked at runtime and can be specified in one of the following ways:
    *   - As a constant. 
    *   - As a functor.
    * Factory has 2 forms of `compound` method so it can be used as a source and as an element of the 
    *   lazy tuple.
    */
    template <class TBoolEval, class TOnTrue, class TOnFalse>
    struct ConditionalFactory : FactoryBase
    {
        //static_assert(std::is_base_of_v<FactoryBase, TOnTrue> && std::is_base_of_v<FactoryBase, TOnFalse>,
        //    "Template arguments TOnTrue and TOnFalse must be FactoryBase implmentations");

        using target_sequence_t = SequenceProxy<
            details::sequence_type_t<TOnTrue>, details::sequence_type_t<TOnFalse> >;

        template <class TEval>
        constexpr ConditionalFactory(TEval&& f, TOnTrue on_true, TOnFalse on_false) noexcept
            : _eval(std::forward<TEval>(f))
            , _on_true(std::move(on_true))
            , _on_false(std::move(on_false))
        {
        }

        constexpr auto compound() const noexcept
        {
            //if you see compile time error there, ensure that 
            //both factories `TOnTrue` and `TOnFalse` produce same type of sequence
            
            if( _eval() )
                return target_sequence_t{ details::get_reference(_on_true).compound() };
            else
                return target_sequence_t{ details::get_reference(_on_false).compound() };
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            //if you see compile time error there, ensure that 
            //both factories `TOnTrue` and `TOnFalse` produce same type of sequence
            if( _eval() )
                return target_sequence_t{ details::get_reference(_on_true).compound(std::move(src)) };
            else
                return target_sequence_t{ details::get_reference(_on_false).compound(std::move(src)) };
        }

        TBoolEval _eval;
        TOnTrue _on_true;
        TOnFalse _on_false;
    };

    template<class A, class B, class C> ConditionalFactory(A, B, C) -> ConditionalFactory<A, B, C>;

}  //ns:OP::flur


#endif //_OP_FLUR_CONDITIONAL__H_
