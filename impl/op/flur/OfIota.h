#pragma once
#ifndef _OP_FLUR_OFIOTA__H_
#define _OP_FLUR_OFIOTA__H_

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
        /** define policy of iota state management when only [begin, end) is specified */ 
        template <class T>
        struct Boundary2
        {
            using value_t = T;

            value_t _begin, _end;
            
            constexpr Boundary2(T begin, T end) noexcept 
                : _begin(std::move(begin))
                , _end(std::move(end))
            {}

            void next(value_t& current) const noexcept 
            {
                ++current;
            }

            bool in_range(const value_t& current) const noexcept
            {
                return current != _end;
            }
        };

        /** define policy of iota state management when only [begin, end) is specified */ 
        template <class T, class U>
        struct Boundary3
        {
            using value_t = T;

            T _begin, _end;
            U _step;

            constexpr Boundary3(T begin, T end, U step) noexcept 
                : _begin(std::move(begin))
                , _end(std::move(end))
                , _step(std::move(step))
            {}

            void next(T& current) const noexcept
            {
                current += _step;
            }

            bool in_range(const T& current) const noexcept
            {
                auto diff = _end - current;
                if( _step > 0 )
                    return !(diff < _step);
                return !(_step < diff);
            }
        };
    }//ns:details

    /**
    *   Create container of sequentially increasing values [begin, end).
    * Container is ordered on condition if for the boundary 
    * [begin, end) condition `(begin <= end)` is true. 
    */
    template <class TBoundaryManagement, class R = typename TBoundaryManagement::value_t>
    struct OfIota : public Sequence<R>
    {
        using this_t = OfIota;
        using boundary_t = TBoundaryManagement;
        using base_t = Sequence<R>;
        using value_t = typename boundary_t::value_t;
        using element_t = typename base_t::element_t;

        explicit constexpr OfIota(boundary_t b) noexcept
            : _boundary{ std::move(b) }
            , _current{_boundary._end} //end
        {
        }

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            if constexpr(OP::has_operators::less_v<element_t>)
            {
                // check begin <= end
                return (_boundary._begin < _boundary._end) || 
                    (_boundary._begin == _boundary._end);
            }
            else 
                return false;
        }

        virtual void start() noexcept override
        {
            _current = _boundary._begin;
        }

        virtual bool in_range() const noexcept override
        {
            return _boundary.in_range(_current);
        }

        virtual element_t current() const override
        {
            return _current;
        }

        virtual void next() override
        {
            _boundary.next(_current);
        }

    private:
        boundary_t _boundary;
        value_t _current;
    };


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFIOTA__H_
