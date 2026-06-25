#pragma once
#ifndef _OP_COMMON_CONDITIONALITERATOR__H_
#define _OP_COMMON_CONDITIONALITERATOR__H_

#include <iterator>

namespace OP::common
{

    /** Very simple mimic to std::back_insert_iterator but allows to filter out values by predicate */
    template< class Container, class Predicate >
    struct filtered_back_insert_iterator
    {
        using difference_type = std::ptrdiff_t; 
        using value_type        = void;
        using pointer           = void;
        using reference         = void;

        using value_t = typename Container::value_type;

        filtered_back_insert_iterator() = delete;

        template <class TP>
        constexpr filtered_back_insert_iterator(Container& container, TP&& predicate) noexcept
            : _container(std::addressof(container))
            , _predicate(std::forward<TP>(predicate))
        {
        }

        constexpr filtered_back_insert_iterator& operator*() noexcept
        { 
            return *this; 
        }

        constexpr filtered_back_insert_iterator& operator=(const value_t& value) 
        {
            if(_predicate(value))
                _container->push_back(value);
            return *this;
        }

        constexpr filtered_back_insert_iterator& operator=(value_t&& value) 
        {
            if(_predicate(value))
                _container->push_back(std::move(value));
            return *this;
        }

        constexpr filtered_back_insert_iterator& operator++() noexcept
        { 
            return *this; 
        }

        constexpr filtered_back_insert_iterator operator++(int) noexcept
        { 
            return *this; 
        }



    private:
        Container* _container;
        Predicate _predicate;
    };

    template <class Container, class Predicate>
    constexpr auto filter_back_inserter(Container& container, Predicate&& predicate) noexcept 
        -> filtered_back_insert_iterator<Container, Predicate>
    {
        return filtered_back_insert_iterator<Container, Predicate>(container, std::forward<Predicate>(predicate));
    }


}//ns:OP::common

#endif //_OP_COMMON_CONDITIONALITERATOR__H_
