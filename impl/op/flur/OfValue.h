#pragma once
#ifndef _OP_FLUR_OFVALUE__H_
#define _OP_FLUR_OFVALUE__H_

#ifdef _MSVC_LANG
// warning C4172: "returning address of local variable or temporary" must be an error
// when trying to return result from function combination
#pragma warning( error: 4172)
#endif //_MSVC_LANG

#include <functional>
#include <memory>
#include <optional>

#include <op/common/Currying.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Ingredients.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /**
    *   Create sequence to iterate over single value. By default it retrieves strictly 1 item.
    * Sequence is treated as an ordred. Manipulating by constructor parameter `limit`
    * you can repeat same value `limit` times.
    */
    template <class T, class R = T>
    struct OfValue : public OrderedSequence<R>
    {
        template < class U = T >
        constexpr OfValue(U&& src, size_t limit = 1)
            :_src(std::forward<U>(src))
            , _limit(limit)
            , _i(0)
        {
        }

        virtual void start() override
        {
            _i = 0;
        }

        virtual bool in_range() const override
        {
            return _i < _limit;
        }

        virtual R current() const override
        {
            return _src;
        }

        virtual void next() override
        {
            ++_i;
        }
    private:
        size_t _i;
        const size_t _limit;
        T _src;
    };

    /**
    *   Create sequence of strictly 1 item that evaluates each time when iteration starts.
    * Sequence is treated as an ordred. 
    */
    template <class T, class F>
    struct OfLazyValue : public OrderedSequence<T>
    {
        using gen_t = F;
        /**Since this sequence created by SimpleFactory need pack arguments to single value*/
        using simple_factory_param_t = std::pair<gen_t, size_t>;

        constexpr OfLazyValue(simple_factory_param_t param) noexcept
            : _attrs(SequenceState{}, param.second)
            , _gen(std::move(param.first))
        {
        }

        virtual void start()
        {
            auto& pline = attr<SequenceState>();
            pline.start();
        }

        virtual bool in_range() const
        {
            return attr<SequenceState>().step().current() < attr<size_t>();
        }

        virtual T current() const
        {
            return _attrs.typed_invoke(_gen);
            //return _gen();
        }

        virtual void next()
        {
            attr<SequenceState>().next();
        }
    private:
        template <class U>
        const auto& attr() const
        {
            return details::get_reference(std::get<U>(_attrs));
        }

        template <class U>
        auto& attr() 
        {
            return details::get_reference(std::get<U>(_attrs));
        }
        /** Store current progress and total limit for this sequence to use as optional attributes
        * for applicator functor
        */
        mutable OP::currying::CurryingTuple<SequenceState, size_t> _attrs;
        mutable gen_t _gen;
    };

    /**
    * Always empty sequence.
    * Sequence is treated as an ordred. 
    * \tparam T is fictive but important to specify type of sequence.
    */
    template <class T>
    struct NullSequence : public OrderedSequence<T>
    {
        constexpr NullSequence() noexcept = default;

        virtual void start() override
        {
            /* do nothing */
        }

        virtual bool in_range() const override
        {
            return false;
        }

        virtual T current() const override
        {
            throw std::out_of_range("resolving item of empty sequence");
        }

        virtual void next() override
        {
            throw std::out_of_range("advance empty sequence");
        }
    };
    template <class T>
    struct NullSequenceFactory : FactoryBase
    {
        constexpr NullSequenceFactory() noexcept = default;
        constexpr NullSequence<T> compound() const noexcept
        {
            return NullSequence<T>{};
        }
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFVALUE__H_
