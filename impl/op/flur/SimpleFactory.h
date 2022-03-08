#pragma once
#ifndef _OP_FLUR_SIMPLEFACTORY__H_
#define _OP_FLUR_SIMPLEFACTORY__H_

#include <functional>
#include <memory>
#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <tuple>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** Helper generic factory that may be used in most cases to create specific Sequence 
    * \tparam V - source container (like std::vector or std::const_reference_wrapper<std::vector ...>)
    * \tparam SourceImpl - target Sequence to construct 
    */
    template <class V, class SourceImpl>
    struct SimpleFactory : FactoryBase
    {
        /**
        * Construct underlayed container from arbitrary parameters
        */
        template <class ... Ux, std::enable_if_t<std::is_constructible_v<V, Ux...>, int> = 0>
        constexpr SimpleFactory(Ux&& ...u) noexcept
            : _v(std::forward<Ux>(u) ...)
        {

        }
        constexpr SimpleFactory(V&& v) noexcept
            : _v(std::forward<V>(v))
        {
        }
        constexpr decltype(auto) compound() const& noexcept
        {
            return SourceImpl(_v);
        }
        constexpr decltype(auto) compound() && noexcept
        {
            return SourceImpl(std::move(_v));
        }
    private:
        V _v;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_SIMPLEFACTORY__H_
