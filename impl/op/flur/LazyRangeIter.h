#pragma once
#ifndef _OP_FLUR_LAZYRANGEITER__H_
#define _OP_FLUR_LAZYRANGEITER__H_

#include <functional>
#include <optional>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** 
    *   Iterator emulator
    *  
    */
    template <class R>
    struct LazyRangeIterator 
    {
        using this_t = LazyRangeIterator<R>;
        using range_t = std::decay_t<R>;
        using target_t = decltype(std::declval<range_t&>().compound());

        using iterator_category = std::forward_iterator_tag;
        using value_type        = decltype(std::declval< target_t>().current());

        LazyRangeIterator (const range_t& r) noexcept
            : _target{ std::move(r.compound()) }
        {
            _target->start();
        }
        /** designated to construct std::end */
        constexpr LazyRangeIterator () noexcept
        {
            
        }
        LazyRangeIterator& operator ++() 
        {
            _target->next();
            return *this;
        }
        /** Note! not all targets supports copy operation so postfix ++ may fail at compile time*/
        LazyRangeIterator operator ++(int) 
        {
            LazyRangeIterator result(*this);
            _target->next();
            return result;
        }
        auto operator *() const
        {
            return _target->current();
        }
        bool operator == (const this_t& right) const
        {
            if( out_of_range() )
                return right.out_of_range();
            return false; //if this has some value no chance this is the same as `right`
        }
        bool operator != (const this_t& right) const
        {
            return !operator==(right);
        }

    private:
        bool out_of_range() const
        {   return !_target || !_target->in_range(); }
        std::optional<target_t> _target;
    };

} //ns:flur
} //ns:OP
#endif //_OP_FLUR_LAZYRANGEITER__H_
