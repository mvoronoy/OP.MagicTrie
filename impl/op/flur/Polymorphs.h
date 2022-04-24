#pragma once
#ifndef _OP_FLUR_POLYMORPHS__H_
#define _OP_FLUR_POLYMORPHS__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    template <class Element>
    struct AbstractPolymorphFactory: FactoryBase
    {
        using element_t = Element;
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
        using polymorph_factory_result_t = std::decay_t<decltype(get_reference(get_reference(std::declval<const Factory&>()).compound()) )>;
        
        template<typename T> struct is_shared_ptr : std::false_type {};
        template<typename T> struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};
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
            typename details::polymorph_factory_result_t<Base>::element_t 
        >
    {
        static_assert( std::is_base_of_v<FactoryBase, Base>,
            "Base must be derived from FactoryBase");
        using base_t = Base;
        using polymorph_base_t = AbstractPolymorphFactory< 
            typename details::polymorph_factory_result_t<Base>::element_t 
        >;

        using element_t = typename polymorph_base_t::element_t;
        using sequence_t = typename polymorph_base_t::sequence_t;
        
        constexpr PolymorphFactory(base_t&& rref) noexcept
            : Base(std::move(rref)) {}

        virtual ~PolymorphFactory() = default;


        std::shared_ptr<typename polymorph_base_t::base_sequence_t> compound_shared() const override
        {
            auto result = base_t::compound();
            using result_seq_t = std::decay_t<decltype(result)>;
            // underlaying factory may already produce shared ptr, so skip wrapping then
            if constexpr(details::is_shared_ptr<result_seq_t>::value )
            {
                return result;
            }
            else
            {
                return std::shared_ptr<typename polymorph_base_t::base_sequence_t>(
                    //uses move constructor
                    new result_seq_t{std::move(result)}
                );
            }
        }
        //void compound(
        //std::unique_ptr<OP::flur::Sequence<const std::basic_string<char,std::char_traits<char>,std::allocator<char>> &>,std::default_delete<OP::flur::Sequence<const std::basic_string<char,std::char_traits<char>,std::allocator<char>> &>>> &) const': is abstract
        //  wit
    };

} //ns: OP::flur

#endif //_OP_FLUR_POLYMORPHS__H_