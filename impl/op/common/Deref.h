#pragma once

#ifndef _OP_COMMON_DEREF__H_
#define _OP_COMMON_DEREF__H_

#include <functional>
#include <memory>


namespace OP::common
{
    template <typename U>
    U& get_reference(std::reference_wrapper<U> u) noexcept 
    {
        return u.get();
    }

    template <typename U>
    U const& get_reference(std::reference_wrapper<U const> u) noexcept 
    {
        return u.get();
    }

    template <typename U>
    constexpr U& get_reference(U& u) noexcept 
    {
        return u;
    }
    
    template <typename U>
    constexpr const U& get_reference(const U& u) noexcept 
    {
        return u;
    }
    
    template <typename U>
    constexpr U&& get_reference(U&& u) noexcept 
    {
        return std::forward<U>(u);
    }

    template <typename U, typename ... Ux>
    U& get_reference(std::unique_ptr<U, Ux ...>& u) noexcept 
    {
        return *u;
    }

    template <typename U, typename ... Ux>
    const U& get_reference(const std::unique_ptr< U, Ux ...>& u) noexcept 
    {
        return *u;
    }

    template <typename U>
    decltype(auto) get_reference(const std::shared_ptr<U>& u) noexcept 
    {
        return *u.get();
    }

    template <typename U>
    decltype(auto) get_reference(std::shared_ptr<U>& u) 
    {
        return *u.get();
    }

    template <typename U>
    U& get_reference(std::shared_ptr<U>&& u) noexcept; /*
    {
        //invoking this code means very-very bad lost control, used only for type-deduction only
        static_assert(false, "lost control over pointer owning");
        return *u.get();
    }*/

    template <typename U, typename ... Ux>
    U& get_reference(std::unique_ptr<U, Ux...>&& u) noexcept;/* 
    {
        //invoking this code means very-very bad lost control, used only for type-deduction only
        static_assert(false, "lost contol over pointer owning");
        return *u.get();
    }*/


    template <class Value>
    using dereference_t = std::decay_t <
        decltype(details::get_reference(std::declval< const Value& >())) >;

}//ns:OP::common

#endif //_OP_COMMON_DEREF__H_
