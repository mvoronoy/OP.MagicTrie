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
    template <class T>
    struct LazyRangeIterator 
    {
        using this_t = LazyRangeIterator<T>;
        using strip_generic_t = std::decay_t<T>;
        //resolve target type of T
        using target_t = strip_generic_t;

        using iterator_category = std::forward_iterator_tag;
        using value_type        = decltype(details::get_reference(std::declval<const target_t&>()).current());
        using difference_type   = std::ptrdiff_t;
        using reference         = value_type&;
        using pointer           = void;

        LazyRangeIterator (target_t&& r) noexcept
            : _target{ std::move(r) }
        {
            details::get_reference(*_target).start();
        }
        /** designated to construct std::end */
        constexpr LazyRangeIterator (nullptr_t) noexcept
        {
            
        }
        LazyRangeIterator& operator ++() 
        {
            details::get_reference(*_target).next();
            return *this;
        }
        /** Note! not all targets supports copy operation so postfix ++ may fail at compile time*/
        LazyRangeIterator operator ++(int) 
        {
            LazyRangeIterator result(*this);
            details::get_reference(*_target).next();
            return result;
        }
        value_type operator *() const
        {         
            return details::get_reference(*_target).current();
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
        {   return !_target || !details::get_reference(*_target).in_range(); }
        std::optional<target_t> _target;
    };

} //ns:flur
} //ns:OP
#endif //_OP_FLUR_LAZYRANGEITER__H_
