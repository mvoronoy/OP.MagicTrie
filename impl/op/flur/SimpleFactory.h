#pragma once
#ifndef _OP_FLUR_SIMPLEFACTORY__H_
#define _OP_FLUR_SIMPLEFACTORY__H_

#include <functional>
#include <memory>
#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <tuple>

namespace OP::flur
{
    /** Helper generic factory that may be used in most cases to create specific Sequence 
    * \tparam Tx - arguments to construct source SourceImpl. For example OfContainer should 
    *    use something like std::vector or std::const_reference_wrapper<std::vector ...>.
    * \tparam SourceImpl - target Sequence to construct 
    */
    template <class SourceImpl, class ... Tx>
    struct SimpleFactory : FactoryBase
    {
        /**
        * Construct underlaid container from arbitrary (at least 1) parameters.
        */
        template <class U, class ... Ux>
        explicit constexpr SimpleFactory(U&& u, Ux&& ...ux) noexcept
            : _v{std::forward_as_tuple( std::forward<U>(u), std::forward<Ux>(ux) ... )}
        {
        }

        constexpr decltype(auto) compound() const& noexcept
        {
            return construct(std::make_index_sequence<sizeof ...(Tx)>{});
        }

        constexpr decltype(auto) compound() && noexcept
        {
            return construct(std::make_index_sequence<sizeof ...(Tx)>{});
        }

    private:

        template <size_t ...Is>
        SourceImpl construct(std::index_sequence<Is...>) const&
        {
            return SourceImpl{ std::get<Is>(_v)... };
        }

        template <size_t ...Is>
        SourceImpl construct(std::index_sequence<Is...>) &&
        {
            return SourceImpl{ std::move(std::get<Is>(_v))... };
        }

        std::tuple<Tx...> _v;
    };

} //ns:OP::flur

#endif //_OP_FLUR_SIMPLEFACTORY__H_
