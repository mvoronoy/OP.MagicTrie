#pragma once
#ifndef _OP_FLUR_LAZYRANGEITER__H_
#define _OP_FLUR_LAZYRANGEITER__H_

#include <op/flur/typedefs.h>

#include <functional>
#include <variant>

namespace OP::flur
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
        using value_type        = 
            
            decltype(details::get_reference(details::get_reference(std::declval<target_t&>())).current());
        using difference_type   = std::ptrdiff_t;
        using reference         = value_type&;
        using pointer           = void;


        LazyRangeIterator (target_t&& r) noexcept
            : _target{std::move(r)}
        {
        }
        /** designated to construct std::end */
        constexpr LazyRangeIterator (nullptr_t) noexcept
            :_target{}
        {
        }

        LazyRangeIterator& operator ++() 
        {
            details::get_reference(get()).next();
            return *this;
        }
        /** Note! not all targets supports copy operation so postfix ++ may fail at compile time*/
        LazyRangeIterator operator ++(int) 
        {
            LazyRangeIterator result(*this);
            details::get_reference(get()).next();
            return result;
        }
        value_type operator *() const
        {         
            return details::get_reference(cget()).current();
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
        { return is_empty() || !details::get_reference(cget()).in_range(); }
        
        const bool is_empty() const
        {
            return std::holds_alternative<std::monostate>(_target);
        }

        target_t& get()
        {
            return std::get<target_t>(_target);
        }
        const target_t& cget() const
        {
            return std::get<target_t>(_target);
        }
        using holder_t = std::variant< std::monostate, target_t>;
        holder_t _target;
    };

} //ns:OP::flur
#endif //_OP_FLUR_LAZYRANGEITER__H_
