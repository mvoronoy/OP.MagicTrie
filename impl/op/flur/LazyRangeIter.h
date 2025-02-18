#pragma once
#ifndef _OP_FLUR_LAZYRANGEITER__H_
#define _OP_FLUR_LAZYRANGEITER__H_

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

#include <functional>
#include <variant>

namespace OP::flur
{
    
    template <class TElement>
    struct AbstractPolymorphFactory; //fwd
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

        constexpr explicit LazyRangeIterator(T r) noexcept
            : _target{std::move(r)}
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
        //LazyRangeIterator operator ++(int) &
        //{
        //    LazyRangeIterator result(*this);
        //    details::get_reference(get()).next();
        //    return result;
        //}

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
    template<class T> LazyRangeIterator (T) -> LazyRangeIterator<T>;

    namespace details
    {

        template <class Factory, std::enable_if_t<std::is_base_of_v<FactoryBase, Factory>, int> = 0 >
        auto begin_impl(const Factory& inst)
        {
            auto postprocess = [](auto ptr){ ptr->start(); return ptr; };
            if constexpr(
                    OP::utils::is_generic<Factory, OP::flur::AbstractPolymorphFactory>::value
                )
            {
                return LazyRangeIterator(postprocess(inst.compound_shared()));
            }
            else
            {
                auto sequence = inst.compound();
                using t_t = decltype(sequence);
                return LazyRangeIterator(postprocess(std::make_shared<t_t>(std::move(sequence))));
            }

        }

        template <class Factory>
        auto begin_impl(const std::shared_ptr<Factory>& inst)
        {
            return begin_impl(*inst);
        }

        template <class Factory>
        constexpr auto end_impl(const Factory* p= nullptr) noexcept
        {
            using result_t = decltype(begin_impl(*p));
            return result_t{nullptr};
        }

    }//ns:details
} //ns:OP::flur
#endif //_OP_FLUR_LAZYRANGEITER__H_
