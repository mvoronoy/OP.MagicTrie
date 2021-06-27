#pragma once
#ifndef _OP_FLUR_REDUCER__H_
#define _OP_FLUR_REDUCER__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRange.h>
#include <op/flur/OfConatiner.h>
#include <op/flur/OfOptional.h>
#include <op/flur/OfValue.h>
#include <op/flur/OfIota.h>

#include <op/flur/Cartesian.h>
#include <op/flur/Filter.h>
#include <op/flur/FlatMapping.h>
#include <op/flur/Mapping.h>
#include <op/flur/OrDefault.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** namespace for some popular reducers (sum, avg ... ) */
    namespace reducer
    {
        /**
        * Reducer of sum.
        * Usage: \code
        * (src::of_iota(0, 5)
        *   ).reduce(OP::flur::reducer::sum) == 10
        * \endcode
        */
        template <class T>
        constexpr auto sum(T ini, T next) noexcept
        {
            return ini + std::move(next);
        }
        /**
        * Reducer of avg.
        * Usage: \code
        * (src::of_container( std::map<char, float>{ {'a', 1.f}, {'b', 1.2f} } )
        *   >> then::mapping((const auto& pair){ return pair.second; })
        *   ).reduce(OP::flur::reducer::avg<float>)
        * \endcode
        */
        /*template <class V>
        struct avg
        {
            size_t _count = 0;
            auto operator() (T ini, T next) noexcept
            {
                return make_lazy_range(
                    SimpleFactory<std::optional<V>, OfOptional<V>>(std::move(v)) );
            }
        }; */

    } //ns:reducer

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_REDUCER__H_
