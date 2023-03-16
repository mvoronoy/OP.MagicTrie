#pragma once
#ifndef _OP_FLUR_LAZYRANGEITER__H_
#define _OP_FLUR_LAZYRANGEITER__H_

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

#include <functional>
#include <variant>

namespace OP::flur
{
    
    /** 
    *   Iterator over LazyRange.
    *  This iterator exposes std::input_iterator_tag that means support forward only
     *  semantic without restart capability.
    */
    template <class T>
    struct LazyRangeIterator 
    {
        using this_t = LazyRangeIterator<T>;
        using target_t = std::decay_t<T>;

        using iterator_category [[maybe_unused]] = std::input_iterator_tag;
        using value_type        =
            decltype(details::get_reference(details::get_reference(std::declval<target_t&>())).current());
        using difference_type   = std::ptrdiff_t;
        using reference         = value_type&;
        using pointer           = void;

        constexpr explicit LazyRangeIterator(T&& r) noexcept
            : _target{std::forward<T>(r)}
        {
        }

        /** designated to construct std::end */
        constexpr explicit LazyRangeIterator(std::nullptr_t) noexcept
            :_target{}
        {
        }

        LazyRangeIterator& operator ++() 
        {
            details::get_reference(get()).next();
            return *this;
        }

        /** Note! not all targets supports copy operation so postfix ++ may fail at compile time*/
        LazyRangeIterator operator ++(int) &
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
        [[nodiscard]] bool out_of_range() const
        { return is_empty() || !details::get_reference(cget()).in_range(); }
        
        [[nodiscard]] constexpr bool is_empty() const
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

    namespace details
    {
        template <class Factory>
        using lazy_iterator_deduction_t = LazyRangeIterator < std::shared_ptr<
                std::decay_t<
                        OP::flur::details::dereference_t< OP::flur::details::unpack_t<Factory> >
                >>
        >;

        template <class Factory>
        auto begin_impl(const Factory& inst)
        {
            auto seq = inst.compound();
            using t_t = std::decay_t<decltype(seq)>;
            using result_t = OP::flur::details::lazy_iterator_deduction_t < Factory >;

            if constexpr (is_shared_ptr<t_t>::value)
            {
                seq->start();
                return result_t(std::move(seq));
            }
            else
            {
                auto seq_ptr = std::make_shared<t_t>(std::move(seq));
                seq_ptr->start();
                return result_t(std::move(seq_ptr));
            }
        }

        template <class Factory>
        constexpr auto end_impl(const Factory* = nullptr) noexcept
        {
            using result_t = OP::flur::details::lazy_iterator_deduction_t < Factory >;
            return result_t{nullptr};
        }
    }//ns:details
} //ns:OP::flur
#endif //_OP_FLUR_LAZYRANGEITER__H_
