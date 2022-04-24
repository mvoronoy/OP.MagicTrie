#pragma once
#ifndef _OP_FLUR_POLYMORPHSBACK__H_
#define _OP_FLUR_POLYMORPHSBACK__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    /**
    *   Create conatiner of strictly 1 item.
    * Container is ordred.
    */
    template <class Holder, class Base>
    struct OfReversePolymorph : public Base
    {
        //static constexpr bool ordered_c = Base::ordered_c;
        using Base::ordered_c;
        using Base::element_t;

        OfReversePolymorph(Holder src)
            : _src(std::move(src))
        {
        }
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return _src->is_sequence_ordered();
        }

        virtual void start()
        {
            _src->start();
        }

        virtual bool in_range() const
        {
            return _src->in_range();
        }

        virtual typename Base::element_t current() const
        {
            return _src->current();
        }

        virtual void next()
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
            auto seq = _polymorph_factory->compound();

            return OfReversePolymorph<decltype(seq), base_seq_t>(std::move(seq));
        }
        Poly _polymorph_factory;
    };

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHSBACK__H_