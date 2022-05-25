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

        template <class Src, class Target, class F>
        struct FlatMapping : public Sequence<typename Target::element_t>
        {
            using src_element_t = typename details::dereference_t<Src>::element_t;

            using base_t = Sequence<typename Target::element_t>;
            using element_t = typename base_t::element_t;

            FlatMapping(Src&& src, F f)
                : _src(std::move(src))
                , _applicator(std::move(f))
            {
            }

            virtual void start()
            {
                details::get_reference(_src).start();
                seek();
            }
            
            virtual bool in_range() const
            {
                return _defered && _defered->in_range();
            }
            
            virtual element_t current() const
            {
                return _defered->current();
            }
            
            virtual void next()
            {
                _defered->next();
                if (!_defered->in_range() && details::get_reference(_src).in_range())
                {
                    details::get_reference(_src).next();
                    seek();
                }
            }
        private:
            void seek()
            {
                auto& rrc = details::get_reference(_src);
                using applicator_res_t = std::decay_t<decltype(_applicator(rrc.current()))>;
                for (; rrc.in_range(); rrc.next())
                {
                    //need to distinguish cases when applicator produces factory or ready to use container

                    if constexpr (std::is_base_of_v<FactoryBase, applicator_res_t>)
                    {
                        _defered.emplace(details::get_reference(_applicator(rrc.current())).compound());
                    }
                    else //assume inherited from Sequence
                    {
                        _defered.emplace(std::move(_applicator(rrc.current())));
                    }
                    _defered->start();
                    if (_defered->in_range())
                        return;
                }
                //reach end of source sequence, no data
                _defered.reset();
            }
            std::optional< Target > _defered;
            Src _src;
            //std::function<Target(const src_element_t&)> _applicator;
            F _applicator;
        };

        template < class F >
        struct FlatMappingFactory : FactoryBase
        {
            constexpr FlatMappingFactory(F&& f) noexcept
                : _applicator(std::forward<F>(f))
            {
            }
            template <class Src>
            constexpr auto compound(Src&& src) const& noexcept
            {
                using src_container_t = details::sequence_type_t<Src>;
                using function_res_t = std::decay_t<decltype(_applicator(details::get_reference(src).current()))>;
                using target_set_t = details::sequence_type_t<function_res_t>;

                return FlatMapping<src_container_t, target_set_t, F>(
                    std::move(src), _applicator);

            }

            template <class Src>
            constexpr auto compound(Src&& src) && noexcept
            {
                using src_container_t = details::sequence_type_t<Src>;
                using function_res_t = std::decay_t<decltype(_applicator(details::get_reference(src).current()))>;
                using target_set_t = details::sequence_type_t<function_res_t>;

                return FlatMapping<src_container_t, target_set_t, F>(
                    std::move(src), std::move(_applicator) );

            }
            F _applicator;
        };


    } //ns:flur
} //ns:OP

#endif //_OP_FLUR_FLATMAPPING__H_
