#pragma once
#ifndef _OP_FLUR_STL_ADAPTERS__H_
#define _OP_FLUR_STL_ADAPTERS__H_
#include <numeric>
#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRange.h>
#include <op/flur/FactoryBase.h>

namespace OP::flur
{
    template <class T, class V = typename details::sequence_type_t<T>::element_t>
    constexpr std::enable_if_t <is_factory_c<T>, V> reduce(const T& inst, V init={}) noexcept
    {
        auto seq = inst.compound();
        auto& rseq = details::get_reference(seq);
        return std::reduce(rseq.begin(), rseq.end(), init);
    }
        
} //ns:OP::flur

namespace std
{
    template <class T>
    constexpr std::enable_if_t <std::is_base_of_v<OP::flur::FactoryBase, T>, bool> empty(const T& inst) noexcept
    {
        using namespace OP::flur::details;
        auto seq = inst.compound();
        auto& rseq = get_reference(seq);
        rseq.start();
        return !rseq.in_range();
    }

    template <class T>
    constexpr bool empty(const OP::flur::Sequence<T>& seq) noexcept
    {
        get_reference(seq).start();
        return !get_reference(seq).in_range();
    }

    template <class T, class UnaryFunction>
    constexpr std::enable_if_t <std::is_base_of_v<OP::flur::FactoryBase, T>, void> for_each(const T& inst, UnaryFunction f) 
    {
        using namespace OP::flur::details;
        auto seq = inst.compound();
        for(get_reference(seq).start(); get_reference(seq).in_range(); get_reference(seq).next())
            f(std::move(get_reference(seq).current()));
    }

} //ns:std
#endif //_OP_FLUR_STL_ADAPTERS__H_