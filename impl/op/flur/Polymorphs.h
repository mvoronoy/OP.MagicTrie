#pragma once
#ifndef _OP_FLUR_POLYMORPHS__H_
#define _OP_FLUR_POLYMORPHS__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    template <bool is_ordered, class Element>
    struct AbstractPolymorphFactory : FactoryBase
    {
        using element_t = Element;
        using sequence_t = std::conditional_t<
            is_ordered, 
            OrderedSequence<element_t>, 
            Sequence<element_t>>;

        virtual ~AbstractPolymorphFactory() = default;
        virtual std::unique_ptr<sequence_t> compound_unique() const = 0;
        virtual std::shared_ptr<sequence_t> compound_shared() const = 0;

        /** 
            As a FactoryBase this abstarction must support `compound()` so 
            it is aliasing of `compound_unique()` 
        */
        virtual std::unique_ptr<sequence_t> compound() const
        {
            return compound_unique();
        }

        auto begin() const
        {
            return LazyRangeIterator< std::shared_ptr<sequence_t> >(compound_shared());
        }
        auto end() const
        {
            return LazyRangeIterator< std::shared_ptr<sequence_t> >(nullptr);
        }


    };
    namespace details
    {
        template <class Factory>
        using polymorph_factory_result_t = std::decay_t<decltype( std::declval<const Factory&>().compound() )>;
    } //ns:details
    /** 
    *   PolymorphFactory is a factory that allows construct other factories on the dynamic memory (heap).
    *   This can be useful when you need polymorph behaviour (in other words 'type erasure").
    *   For example you can define virtual method (that expects well defined signature instead 
    *   of template definitions) that 
    */
    template <class Base>
    struct PolymorphFactory : Base,
        AbstractPolymorphFactory< 
            details::polymorph_factory_result_t<Base>::ordered_c, 
            typename details::polymorph_factory_result_t<Base>::element_t 
        >
    {
        static_assert( std::is_base_of_v<FactoryBase, Base>,
            "Base must be derived from FactoryBase");
        using base_t = Base;
        using polymorph_base_t = AbstractPolymorphFactory< 
            details::polymorph_factory_result_t<Base>::ordered_c, 
            typename details::polymorph_factory_result_t<Base>::element_t 
        >;

        using element_t = typename polymorph_base_t::element_t;
        using sequence_t = typename polymorph_base_t::sequence_t;
        
        constexpr PolymorphFactory(base_t&& rref) noexcept
            : Base(std::move(rref)) {}

        virtual ~PolymorphFactory() = default;

        std::unique_ptr<sequence_t> compound_unique() const override
        {
            auto result = base_t::compound();
            using t_t = std::decay_t<decltype(result)>;
            return std::unique_ptr<sequence_t>(new t_t{std::move(result)});
        }
        std::shared_ptr<sequence_t> compound_shared() const override
        {
            auto result = base_t::compound();
            using t_t = std::decay_t<decltype(result)>;
            return std::shared_ptr<sequence_t>(new t_t{std::move(result)});
        }
    };

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHS__H_