#pragma once
#ifndef _OP_FLUR_SEQUENCE__H_
#define _OP_FLUR_SEQUENCE__H_

#include <functional>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** Base for all classes that composes result Sequence */
    struct FactoryBase {};
    
    /** Abstraction that encapsulate iteration capability without specification of source container.
     \tparam T type of element resolved during iteration
     */
    template <class T>
    struct Sequence
    {
        /** Compile time constant indicates Sequence over sorted sequence */
        static constexpr bool ordered_c = false;

        using this_t = Sequence<T>;

        /** Type of element */
        using element_t = T;

        /** Start iteration from the beginning. If iteration was already in progress it resets.  */
        virtual void start() = 0;
        /** Check if Sequence is in valid position and may call `next` safely */
        virtual bool in_range() const = 0;
        /** Return current item */
        virtual element_t current() const = 0;
        /** Position iterable to the next step */
        virtual void next() = 0;

    };

    /** Provide definition for ordered iteable sequence */
    template <class T>
    struct OrderedSequence : Sequence<T>
    {
        /** Compile time constant indicates Sequence over sorted sequence */
        static constexpr bool ordered_c = true;
        using base_t = Sequence<T>;
        using this_t = OrderedSequence<T>;
        using element_t = typename base_t::element_t;
    };
}//ns:flur
}//ns:OP

#endif //_OP_FLUR_SEQUENCE__H_