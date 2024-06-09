#pragma once
#ifndef _OP_FLUR_BOOKMARK__H_
#define _OP_FLUR_BOOKMARK__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/Utils.h>

namespace OP::flur
{
    template <auto Key, class T>
    struct Bookmark
    {
        using key_t = decltype(Key);
        
        constexpr static key_t key_c = Key;

        virtual T value() const noexcept = 0;
    };

    namespace details
    {
        template <class T>
        constexpr const T* _bmk()
        {
            T* r = nullptr;
            return r;
        }

        template <class T, auto k, class U>
        constexpr bool _check_is_bookmark(const Bookmark<k, U>* a) noexcept
        {
            return std::is_base_of_v<std::decay_t<decltype(*a)>, T>;
        }

        template <class T>
        constexpr bool _check_is_bookmark(...) noexcept
        {
            return false;
        }
    } //ns:details

    /** \brief allows check at compile-time if reference type `T` is OP::flur::Bookmark */
    template <class T>
    constexpr static inline bool is_bookmark_c = details::_check_is_bookmark<T>(static_cast<const T*>(nullptr));

    /** \brief Predicate used together with OP::utils::type_filter_t to demarcate types inherited from Bookmark */
    struct PredicateSelectBookmarks
    {
        template <class T>
        static constexpr bool check = is_bookmark_c<std::decay_t<T>>;
    }; 

    struct AttributeManager
    {
    };

} //ns:OP::flur

#endif //_OP_FLUR_BOOKMARK__H_
