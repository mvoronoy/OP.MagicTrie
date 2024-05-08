#pragma once
#ifndef _OP_FLUR_POLYMORPHS__H_
#define _OP_FLUR_POLYMORPHS__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    template <class TElement>
    struct AbstractPolymorphFactory: FactoryBase
    {
        using element_t = TElement;
        using sequence_t = Sequence<element_t>;
        using base_sequence_t = sequence_t;

        AbstractPolymorphFactory() = default;
        AbstractPolymorphFactory(const AbstractPolymorphFactory&) = delete;
        AbstractPolymorphFactory& operator=(const AbstractPolymorphFactory&) = delete;

        virtual ~AbstractPolymorphFactory() = default;

        virtual std::shared_ptr<sequence_t> compound_shared() const = 0;

        /** 
            As a FactoryBase this abstarction must support `compound()` so 
            it is aliasing of `compound_shared()` 
        */
        std::shared_ptr<sequence_t> compound() const 
        {
            return compound_shared();
        }

        auto begin() const
        {
            return details::begin_impl(*this);
        }
        auto end() const
        {
            return details::end_impl(this);
        }
    
    };
    

    /** 
    *   PolymorphFactory is a factory that allows construct other factories on the heap.
    *   This can be useful when you need polymorph behaviour (with 'type erasure" up to Sequnce<T> ).
    *   For example you can define virtual method (that expects well defined signature instead 
    *   of template definitions).
    *   \tparam TFactory - some FactoryBase descendant to hide behind polymorph behavior.
    */
    template <class TFactory>
    struct PolymorphFactory : 
        AbstractPolymorphFactory<details::sequence_element_type_t<TFactory>>
    {
        using base_factory_t = std::decay_t<TFactory>;
        static_assert( std::is_base_of_v<FactoryBase, base_factory_t>,
            "Base must be derived from FactoryBase");
        using sequence_t = details::sequence_type_t<base_factory_t>;
        using base_t = AbstractPolymorphFactory<  
            details::sequence_element_type_t<TFactory>
        >;

        constexpr PolymorphFactory(base_factory_t&& rref) noexcept
            : _base_factory(std::move(rref)) {}

        virtual ~PolymorphFactory() = default;


        std::shared_ptr<typename base_t::sequence_t> compound_shared() const override
        {
            auto result = _base_factory.compound();
            using result_seq_t = std::decay_t<decltype(result)>;
            // underlaying factory may already produce shared ptr, so skip wrapping then
            if constexpr(details::is_shared_ptr<result_seq_t>::value )
            {
                return result;
            }
            else
            {
                return std::shared_ptr<typename base_t::sequence_t>(
                    //uses move constructor
                    new result_seq_t{std::move(result)}
                );
            }
        }
    private:
        base_factory_t _base_factory;
    };
    template<class TFactory> PolymorphFactory(TFactory) -> PolymorphFactory<TFactory>;

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHS__H_