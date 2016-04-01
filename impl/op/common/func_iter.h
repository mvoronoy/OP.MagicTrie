#ifndef _OP_FUNC_ITER__H_
#define _OP_FUNC_ITER__H_

#include <iterator>
#include <tuple>
#include <op/common/Utils.h>

namespace OP
{


template <typename A, typename B>
struct pack_to_tuple
{
    using type = std::tuple<A, B>;
    static inline type make(A&& a, B&& b)
    {
        return std::make_tuple(std::forward<A>(a), std::forward<B>(b));
    }
};

template <typename A, typename... Args>
struct pack_to_tuple<A, std::tuple<Args...> > 
{
    using type = std::tuple<A, Args...>;
    static inline type make(A&& a, std::tuple<Args...>&& b)
    {
        return std::tuple_cat(std::forward_as_tuple(a), std::move(b) );
    }
};
template <typename... Args, typename B>
struct pack_to_tuple<std::tuple<Args...>, B > 
{
    using type = std::tuple<Args..., B>;
    static inline type make(std::tuple<Args...>&& a, B&& b)
    {
        return std::tuple_cat(a, std::forward_as_tuple(b) );
    }
};

/**
*   Iterator that takes another iterator and wrap result of dereferencing with arbitrary functor.
*   This allows create chained evaluation similar to functional languages `map`.
*   Simple usage: \code
*   std::string src("abcdefg");
*   
*   auto i = make_func_iterator(src.begin(), [](char symbol)->int{return (int)symbol;});
*   //print symbol and it char-code
*   for(; i != src.end(); ++i)
*   {
*       auto v = *i; //perform evaluation only on dereferencing and get tuple<char, int>
*       std::cout << std::get<0>(v) << '=' << std::get<1>(v) << '\n';
*   }
*   \endcode
*   Iterator exposes compare operators that keep aility to compare with origin, see in example
*   above `i != src.end()`
*/
template <class I, class UnaryFunction>
struct expanding_func_iterator : public std::iterator<
        typename I::iterator_category,
        typename pack_to_tuple<typename I::value_type, typename std::result_of<UnaryFunction(typename I::value_type)>::type >::type,
        typename I::difference_type    
    >
{
    using this_t = expanding_func_iterator <I, UnaryFunction>;
     
    expanding_func_iterator(I base, UnaryFunction map)
        : _base(std::forward<I>(base))
        , _map(std::forward<UnaryFunction>(map))
        {}
    /** Create new iterator from copy of this one by combining withe yet another functor. Use
     *  syntax sugar '|' to to the same 
     * \tparam UnaryFunction2 functor that accepts this_t::value_type as an input argument
     */
    template <class UnaryFunction2> 
    expanding_func_iterator <this_t, UnaryFunction2> combine(UnaryFunction2 && func2) const
    {
        return expanding_func_iterator <this_t, UnaryFunction2>(*this, std::forward<UnaryFunction2>(func2));
    }

    /** Just syntax sugar over #combine */
    template <class UnaryFunction2> 
    expanding_func_iterator <this_t, UnaryFunction2> operator | (UnaryFunction2 && func2) const
    {
        return combine(std::forward<UnaryFunction2>(func2));
    }

    this_t& operator ++ ()
    {
        ++_base;
        return *this;    
    }
    this_t operator ++ (int)
    {
        auto r(*this);
        ++_base;
        return r;    
    }
    value_type operator *() const
    {
        auto r0 = *_base;
        auto r1 = _map(r0);
        return pack_to_tuple<decltype(r0), decltype(r1)>::make(std::move(r0), std::move(r1));
    }
    //allow compare with origin iterator
    bool operator == (const I& other) const
    {
        return _base == other;
    }
    bool operator == (const this_t& other) const
    {
        return operator == (other._base);
    }
    template <class I2>
    bool operator == (const I2& other) const
    {
        return _base.operator == (other);
    }
    bool operator != (const I& other) const
    {
        return _base != other;
    }
    template <class I2>
    bool operator != (const I2& other) const
    {
        return _base.operator != (other);
    }
    bool operator != (const this_t& other) const
    {
        return operator != (other._base);
    }


private:
    I _base;
    UnaryFunction _map;    
};
/**Simplifies creation of make_expanding_func_iterator*/
template <class I, class UnaryFunction>
inline expanding_func_iterator <I, UnaryFunction> make_expanding_func_iterator(I && i, UnaryFunction && map)
{
    return expanding_func_iterator <I, UnaryFunction>(std::forward<I>(i), std::forward<UnaryFunction>(map));
}

template <class I, class UnaryFunction>
struct func_iterator : public std::iterator<
    typename I::iterator_category,
    typename std::result_of<UnaryFunction(typename I::value_type)>::type,
    typename I::difference_type
>
{
    using this_t = func_iterator<I, UnaryFunction>;

    func_iterator(I base, UnaryFunction map)
        : _base(std::forward<I>(base))
        , _map(std::forward<UnaryFunction>(map))
    {}
    I& origin()
    {
        return _base;
    }
    const I& origin() const
    {
        return _base;
    }
    /** Create new iterator from copy of this one by combining withe yet another functor. Use
    *  syntax sugar '|' to to the same
    * \tparam UnaryFunction2 functor that accepts this_t::value_type as an input argument
    */
    template <class UnaryFunction2>
    func_iterator<this_t, UnaryFunction2> combine(UnaryFunction2 && func2) const
    {
        return func_iterator<this_t, UnaryFunction2>(*this, std::forward<UnaryFunction2>(func2));
    }

    /** Just syntax sugar over #combine */
    template <class UnaryFunction2>
    func_iterator<this_t, UnaryFunction2> operator | (UnaryFunction2 && func2) const
    {
        return combine(std::forward<UnaryFunction2>(func2));
    }

    this_t& operator ++ ()
    {
        ++_base;
        return *this;
    }
    this_t operator ++ (int)
    {
        auto r(*this);
        ++_base;
        return r;
    }
    value_type operator *() const
    {
        auto r0 = *_base;
        auto r1 = _map(r0);
        return pack_to_tuple<decltype(r0), decltype(r1)>::make(std::move(r0), std::move(r1));
    }
    //allow compare with origin iterator
    bool operator == (const I& other) const
    {
        return _base == other;
    }
    bool operator == (const this_t& other) const
    {
        return operator == (other._base);
    }
    template <class I2>
    bool operator == (const I2& other) const
    {
        return _base.operator == (other);
    }
    bool operator != (const I& other) const
    {
        return _base != other;
    }
    template <class I2>
    bool operator != (const I2& other) const
    {
        return _base.operator != (other);
    }
    bool operator != (const this_t& other) const
    {
        return operator != (other._base);
    }

private:
    I _base;
    UnaryFunction _map;
};

/**Simplifies creation of make_expanding_func_iterator*/
template <class I, class UnaryFunction>
inline func_iterator <I, UnaryFunction> make_func_iterator(I && i, UnaryFunction && map)
{
    return func_iterator <I, UnaryFunction>(std::forward<I>(i), std::forward<UnaryFunction>(map));
}

}//ns:OP
#endif //_OP_FUNC_ITER__H_
