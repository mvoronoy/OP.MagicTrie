#pragma once
#ifndef _OP_FLUR_OFOPTIONAL__H_
#define _OP_FLUR_OFOPTIONAL__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    namespace details
    {
        template <class TContainer>
        using optional_element_type_t = std::decay_t<decltype(*std::declval<TContainer>())>;

        template <class TContainer>
        using cref_optional_element_type_t = std::add_const_t<std::add_lvalue_reference_t<optional_element_type_t<TContainer>>>;
    }//ns:details
    /**
    * Treate T as container of 0 or 1 element of some underlayed type.
    * 
    * The container must be a type that:
    * - Can be contextually converted to `bool`, 
    * - Supports the `*` (dereference) operator.
    *
    * Typical examples include: `std::optional`, `std::shared_ptr`, and `std::unique_ptr`.
    * Note, the result sequence is sorted.
    */
    template <class T>
    struct OfOptional : public OrderedSequence<details::optional_element_type_t<T>>
    {
        
        using typename OrderedSequence::element_t;

        explicit constexpr OfOptional(T src) noexcept
            : _src(std::move(src))
            , _retrieved(false)
        {
        }

        virtual void start() override
        {
            _retrieved = false;
        }

        virtual bool in_range() const override
        {
            return !_retrieved && static_cast<bool>(_src);
        }

        virtual element_t current() const override
        {
            return *_src;
        }

        virtual void next() override
        {
            _retrieved = true;
        }

    private:
        bool _retrieved;
        T _src;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFOPTIONAL__H_
