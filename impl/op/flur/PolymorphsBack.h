#pragma once
#ifndef _OP_FLUR_POLYMORPHSBACK__H_
#define _OP_FLUR_POLYMORPHSBACK__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Proxy.h>

namespace OP::flur
{

    template < class Poly >
    struct OfReversePolymorphFactory : FactoryBase
    {
        explicit constexpr OfReversePolymorphFactory(Poly factory) noexcept
            : _polymorph_factory(std::move(factory))
        {
        }

        constexpr auto compound() const noexcept
        {
            auto seq = details::get_reference(_polymorph_factory).compound();
            using proxy_t = SequenceProxy< std::decay_t<decltype(seq)> >;
            return proxy_t(std::move(seq));
        }

    private:
        Poly _polymorph_factory;
    };

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHSBACK__H_