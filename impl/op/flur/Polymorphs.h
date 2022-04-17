#pragma once
#ifndef _OP_FLUR_POLYMORPHS__H_
#define _OP_FLUR_POLYMORPHS__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    template <bool is_ordered, class Element>
    struct AbstractPolymorphFactory;
    
    template <class Element>
    struct AbstractPolymorphFactory<false, Element> : FactoryBase
    {
        using element_t = Element;
        using sequence_t = Sequence<element_t>;
        using base_sequence_t = sequence_t;
        constexpr static bool ordered_c = false;

        AbstractPolymorphFactory() = default;
        AbstractPolymorphFactory(const AbstractPolymorphFactory&) = delete;
        AbstractPolymorphFactory& operator=(const AbstractPolymorphFactory&) = delete;

        virtual ~AbstractPolymorphFactory() = default;
        
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const 
        {
            return false;
        }
        std::unique_ptr<sequence_t> compound_unique() const 
        {
            std::unique_ptr<sequence_t> target;
            compound(target);
            return target;
        }
        std::shared_ptr<sequence_t> compound_shared() const 
        {
            std::unique_ptr<sequence_t> target;
            compound(target);
            return std::shared_ptr<sequence_t>(std::move(target));
        }

        /** 
            As a FactoryBase this abstarction must support `compound()` so 
            it is aliasing of `compound_shared()` 
        */
        std::shared_ptr<sequence_t> compound() const 
        {
            return compound_shared();
        }
        virtual void compound(std::unique_ptr<base_sequence_t>& target) const = 0;

        auto begin() const
        {
            return LazyRangeIterator< std::shared_ptr<sequence_t> >(compound_shared());
        }
        auto end() const
        {
            return LazyRangeIterator< std::shared_ptr<sequence_t> >(nullptr);
        }
    
    };
    
    template <class Element>
    struct AbstractPolymorphFactory<true, Element> : public AbstractPolymorphFactory<false, Element>
    {
        using unordered_base_t = AbstractPolymorphFactory<false, Element>;

        using sequence_t = OrderedSequence<element_t>;
        constexpr static bool ordered_c = true;
        
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return true;
        }

        // Overload, not override
        std::unique_ptr<sequence_t> compound_unique() const 
        {
            auto base_ptr = unordered_base_t::compound_unique();
            return std::unique_ptr<sequence_t>(
                static_cast<sequence_t*>(base_ptr.release()));
        }
        // Overload, not override
        std::shared_ptr<sequence_t> compound_shared() const 
        {
            return std::static_pointer_cast<sequence_t>(
                std::move(unordered_base_t::compound_shared()));
        }
        // Overload, not override
        std::shared_ptr<sequence_t> compound() const 
        {
            return compound_shared();
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
            details::does_factory_make_ordered_c<Base>, 
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
        constexpr static bool ordered_c = polymorph_base_t::ordered_c;

        using element_t = typename polymorph_base_t::element_t;
        using sequence_t = typename polymorph_base_t::sequence_t;
        
        constexpr PolymorphFactory(base_t&& rref) noexcept
            : Base(std::move(rref)) {}

        virtual ~PolymorphFactory() = default;


        void compound(std::unique_ptr<typename polymorph_base_t::base_sequence_t>& target) const override
        {
            auto result = base_t::compound();
            using t_t = std::decay_t<decltype(result)>;
            target.reset(new t_t{std::move(result)});
        }
        //void compound(
        //std::unique_ptr<OP::flur::Sequence<const std::basic_string<char,std::char_traits<char>,std::allocator<char>> &>,std::default_delete<OP::flur::Sequence<const std::basic_string<char,std::char_traits<char>,std::allocator<char>> &>>> &) const': is abstract
        //  wit
    };

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHS__H_