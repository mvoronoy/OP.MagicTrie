#pragma once
#ifndef _OP_FLUR_SEQUENCE__H_
#define _OP_FLUR_SEQUENCE__H_

#include <functional>
#include <op/flur/typedefs.h>
#include <op/flur/LazyRangeIter.h>

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
        
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const
        {
            return false;
        }

        /** Start iteration from the beginning. If iteration was already in progress it resets.  */
        virtual void start() = 0;
        /** Check if Sequence is in valid position and may call `next` safely */
        virtual bool in_range() const = 0;
        /** Return current item */
        virtual element_t current() const = 0;
        /** Position iterable to the next step */
        virtual void next() = 0;

        auto begin()
        {
            using t_t = std::reference_wrapper<this_t>;
            start();
            return LazyRangeIterator<t_t>(std::ref(*this));
        }
        auto end()
        {
            using t_t = std::reference_wrapper<this_t> ;
            return LazyRangeIterator<t_t>(nullptr);
        }
        //auto begin() const
        //{
        //    using t_t = std::reference_wrapper<this_t const> ;
        //    return LazyRangeIterator<t_t>(std::cref(*this));
        //}
        //auto end() const
        //{
        //    using t_t = std::reference_wrapper<this_t const> ;
        //    return LazyRangeIterator<t_t>(nullptr);
        //}
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
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return true;
        }
    };


}//ns:flur
}//ns:OP

#endif //_OP_FLUR_SEQUENCE__H_
