#pragma once
#ifndef _OP_FLUR_POLYMORPHSBACK__H_
#define _OP_FLUR_POLYMORPHSBACK__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    /**
    *   Create sequence that is proxy around std::shared_ptr<Sequence....>
    */
    template <class Holder, class Base>
    struct OfReversePolymorph : public Base
    {
        //static constexpr bool ordered_c = Base::ordered_c;
        using Base::ordered_c;
        using typename Base::element_t;

        OfReversePolymorph(Holder src)
            : _src(std::move(src))
        {
        }

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            return _src->is_sequence_ordered();
        }

        void start() override
        {
            _src->start();
        }

        bool in_range() const override
        {
            return _src->in_range();
        }

        typename Base::element_t current() const override
        {
            return _src->current();
        }

        void next() override
        {
            _src->next();
        }
    private:
        Holder _src;
    };

    template < class Poly >
    struct OfReversePolymorphFactory : FactoryBase
    {
        using base_seq_t = typename OP::flur::details::dereference_t<Poly>::sequence_t;
        
        constexpr OfReversePolymorphFactory(Poly factory) noexcept
            : _polymorph_factory(std::move(factory))
        {
        }

        constexpr auto compound() const noexcept
        {
            auto seq = details::get_reference(_polymorph_factory).compound();

            return OfReversePolymorph<decltype(seq), base_seq_t>(std::move(seq));
        }
        Poly _polymorph_factory;
    };

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHSBACK__H_