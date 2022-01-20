#pragma once
#ifndef _OP_FLUR_STL_ADAPTERS__H_
#define _OP_FLUR_STL_ADAPTERS__H_

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRange.h>

namespace OP::flur
{
    template <class T, class V = typename details::sequence_type_t<T>::element_t>
    constexpr std::enable_if_t <std::is_base_of_v<OP::flur::FactoryBase, T>, V> reduce(const T& inst, V init={}) noexcept
    {
        auto seq = inst.compound();
        return std::reduce(seq.begin(), seq.end(), init);
    }
} //ns:OP::flur
namespace std
{
    template <class T>
    constexpr std::enable_if_t <std::is_base_of_v<OP::flur::FactoryBase, T>, bool> empty(const T& inst) noexcept
    {
        using namespace OP::flur::details;
        auto seq = inst.compound();
        get_reference(seq).start();
        return !seq.in_range();
    }

    template <class T>
    constexpr bool empty(const OP::flur::Sequence<T>& seq) noexcept
    {
        get_reference(seq).start();
        return !seq.in_range();
    }
} //ns:std
#endif //_OP_FLUR_STL_ADAPTERS__H_