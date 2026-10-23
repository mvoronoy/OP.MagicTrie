#pragma once
#ifndef _OP_COMMON_CONDITIONALITERATOR__H_
#define _OP_COMMON_CONDITIONALITERATOR__H_

#include <iterator>

namespace OP::common
{
    namespace
    {
        enum class method_id
        {
            push_back,
            insert
        };
    }
    /** Very simple mimic to std::back_insert_iterator but allows to filter out values by predicate */
    template< class Container, class Predicate, method_id mid >
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
            if (_predicate(value))
            {
                if constexpr(mid == method_id::insert)
                    _container->insert(value);
                else
                    _container->push_back(value);
            }
            return *this;
        }

        constexpr filtered_back_insert_iterator& operator=(value_t&& value) 
        {
            if (_predicate(value))
            {
                if constexpr (mid == method_id::insert)
                    _container->insert(std::move(value));
                else
                    _container->push_back(std::move(value));
            }
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
    {
        using out_iterator_t =
            filtered_back_insert_iterator<Container, std::decay_t<Predicate>, method_id::push_back>;
        // Validate against the C++20 output_iterator concept at compile time
        //static_assert(
        //    std::output_iterator<out_iterator_t, typename Container::value_type>);


        return out_iterator_t(
            container, std::forward<Predicate>(predicate));
    }


    template <class Container, class Predicate>
    constexpr auto filter_inserter(Container& container, Predicate&& predicate) noexcept 
    {
        using out_iterator_t =
            filtered_back_insert_iterator<Container, Predicate, method_id::insert>;
        // Validate against the C++20 output_iterator concept at compile time
        static_assert(
            std::output_iterator<out_iterator_t, typename Container::value_type>);

        return out_iterator_t(
            container, std::forward<Predicate>(predicate));
    }


}//ns:OP::common

#endif //_OP_COMMON_CONDITIONALITERATOR__H_
