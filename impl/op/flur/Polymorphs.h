#pragma once
#ifndef _OP_FLUR_POLYMORPHS__H_
#define _OP_FLUR_POLYMORPHS__H_

#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    template <class TElement>
    struct AbstractPolymorphFactory
        : FactoryBase
        , std::enable_shared_from_this<AbstractPolymorphFactory<TElement>>
    {
        using element_t = TElement;
        using sequence_t = OP::flur::Sequence<TElement>;
        using base_sequence_t = sequence_t;
        using this_t = AbstractPolymorphFactory<TElement>;
        using this_ptr = std::shared_ptr<this_t>;
        using this_cptr = std::shared_ptr<this_t const>;

        struct SequenceDeleter
        {
            this_cptr _factory;
            
            explicit SequenceDeleter(this_cptr factory) noexcept
                : _factory{factory}
            {
            }

            void operator()(sequence_t* ptr)
            { 
                delete ptr; 
                _factory.reset(); //release factory counter
            }
        };

        using sequence_ptr = std::unique_ptr<sequence_t, SequenceDeleter>;

        AbstractPolymorphFactory() = default;
        AbstractPolymorphFactory(const AbstractPolymorphFactory&) = delete;
        AbstractPolymorphFactory& operator=(const AbstractPolymorphFactory&) = delete;

        virtual ~AbstractPolymorphFactory() = default;

        virtual sequence_ptr compound_unique() const = 0;

        virtual std::shared_ptr<sequence_t> compound_shared() const = 0;

        /** 
            As a FactoryBase this abstraction must support `compound()` so 
            it is aliasing of `compound_unique()` 
        */
        sequence_ptr compound() const 
        {
            return compound_unique();
        }

        auto begin() const
        {
            return details::begin_impl(this->shared_from_this());
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

        using base_t = AbstractPolymorphFactory<  
            details::sequence_element_type_t<TFactory>
        >;
        using sequence_ptr = typename base_t::sequence_ptr;
        using sequence_t = typename base_t::sequence_t;
        using sequence_deleter_t = typename base_t::SequenceDeleter;

        explicit constexpr PolymorphFactory(base_factory_t&& rref) noexcept
            : _base_factory(std::move(rref)) 
        {
        }

        sequence_ptr compound_unique() const override
        {
            auto result = _base_factory.compound();
            using result_seq_t = std::decay_t<decltype(result)>;
            // underlaying factory may already produce shared ptr, so skip wrapping then
            if constexpr(details::is_unique_ptr<result_seq_t>::value )
            {
                return result;
            }
            else
            {
                return sequence_ptr(
                    //uses move constructor
                    new result_seq_t{std::move(result)},
                    // custom deleter to keep factory captured
                    sequence_deleter_t{ this->shared_from_this() }
                );
            }
        }
        
        std::shared_ptr<sequence_t> compound_shared() const override
        {
            auto result = _base_factory.compound();
            using result_seq_t = std::decay_t<decltype(result)>;
            return std::shared_ptr<sequence_t>(
                    //uses move constructor
                    new result_seq_t{std::move(result)},
                    // custom deleter to keep factory captured
                    sequence_deleter_t{ this->shared_from_this() }
                );
        }

    private:
        base_factory_t _base_factory;
    };
    template<class TFactory> explicit PolymorphFactory(TFactory) -> PolymorphFactory<TFactory>;

} //ns: OP::flur

namespace std
{
    
    template <class T>
    auto begin(std::shared_ptr<OP::flur::AbstractPolymorphFactory<T>> inst)
    {
        return inst->begin();
    }

    template <class T>
    auto end(std::shared_ptr<OP::flur::AbstractPolymorphFactory<T>> inst) noexcept
    {
        return inst->end();
    }
      
}//ns:std

#endif //_OP_FLUR_POLYMORPHS__H_