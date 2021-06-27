#pragma once
#ifndef _OP_FLUR_FLATMAPPING__H_
#define _OP_FLUR_FLATMAPPING__H_

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

    template <class Src, class Target>
    struct FlatMapping : public Sequence<typename Target::element_t>
    {
        using result_t = typename Target::element_t;

        template <class F>
        FlatMapping(Src&& src, F f)
            :_src(std::move(src))
            , _applicator(std::move(f))
        {
        }

        virtual void start()
        {
            _src.start();
            seek();
        }
        virtual bool in_range() const
        {
            return _defered && _defered->in_range();
        }
        virtual result_t current() const
        {
            return _defered->current();
        }
        virtual void next()
        {
            _defered->next();
            if (!_defered->in_range() && _src.in_range())
            {
                _src.next();
                seek();
            }
        }
    private:
        void seek()
        {
            for (; _src.in_range(); _src.next())
            {
                _defered.emplace(details::unpack(_applicator(_src.current())));
                _defered->start();
                if (_defered->in_range())
                    return;
            }
            //reach end of source sequence, no data
            _defered.reset();
        }
        std::optional< Target > _defered;
        Src _src;
        std::function<Target(const typename Src::element_t&)> _applicator;
    };

    template < class F >
    struct FlatMappingFactory : FactoryBase
    {
        constexpr FlatMappingFactory(F f) noexcept
            : _applicator(std::move(f))
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_container_t = details::unpack_t<Src>;
            using function_res_t = decltype(_applicator(std::declval<src_container_t>().current()));
            using target_set_t = details::unpack_t<function_res_t>;
            using element_t = typename target_set_t::element_t;
            //need to distinguish cases when applicator produces factory or ready to use container
            if constexpr (std::is_base_of_v<FactoryBase, function_res_t>)
            {
                return FlatMapping<src_container_t, target_set_t>(
                    std::move(details::unpack(std::move(src))),
                    [this](const auto& factory) {return details::unpack(_applicator(factory)); });
            }
            else
            {
                //std::cout << "target-set for flat-map:" << typeid(target_set_t).name() << ", produce type:" << "\n";
                //return OfValue<int>(75);
                return FlatMapping<src_container_t, target_set_t>(
                    std::move(details::unpack(std::move(src))),
                    _applicator);
            }
        }
        F _applicator;
    };


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_FLATMAPPING__H_
