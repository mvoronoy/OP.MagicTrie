#pragma once
#ifndef _OP_FLUR_ZIP__H_
#define _OP_FLUR_ZIP__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    /** \brief almost the same as `std::optional` but allows to deal with references as well */
    template <class T>
    class zip_opt : public std::optional<T>
    {
        using base_t = std::optional<T>;
    public:
        using base_t::base_t;
    };

    template <class T>
    class zip_opt<T&> : public std::optional<std::reference_wrapper<T>>
    {
        using base_t = std::optional<std::reference_wrapper<T>>;
    public:
        using base_t::base_t;
    
        zip_opt(T& t)
            : base_t(std::ref(t))
            {}
        
        constexpr const T& operator*() const
        {
            return base_t::operator*().get();
        }

        constexpr T& operator*() 
        {
            return base_t::operator*().get();
        }
    };

    namespace details
    {
        template <class TSeq>
        using zip_t = zip_opt<sequence_element_type_t<TSeq>>;
    } //ns:details

    /** 
     * \brief Combine multiple sequences using the zip algorithm and produce elements as a result of applying `F`.
     *
     *  Zip works on two or more sequences, combining them sequentially as multiple arguments to the `F` applicator. 
     *  For example:
     *  \code
     *   src::zip(
     *       [](int i, char c) -> std::string { // Convert zipped pair to string
     *               std::ostringstream result;
     *               result << '[' << i << ", " << c << ']';
     *               return result.str();
     *       }, 
     *       src::of_container(std::array{1, 2, 3}),
     *       src::of_container(std::array{'a', 'b', 'c', 'd'}) //'d' will be omitted
     *   )
     *   >>= apply::for_each([](const std::string& r) { std::cout << r << "\n"; });
     *  \endcode
     *  This prints: \code
     * [1, a]
     * [2, b]
     * [3, c] \endcode
     * 
     *  Note that zip operates until the smallest sequence is exhausted, so you cannot control the trailing 
     *  elements of longer sequences. 
     *  To process all elements in the longest sequence use `zip_longest`, this way you need wrap all arguments of 
     *  your applicator with `zip_opt`. This gives you control if sequence is exhausted. For example, a 3-sequence zip 
     *  with optional arguments: \code
     *   using namespace std::string_literals;
     *   auto print_optional = [](std::ostream& os, const auto& v) -> std::ostream& 
     *      { return v ? (os << *v) : (os << '?'); };
     *   src::of_container(std::array{1, 2, 3})
     *       >> then::zip_longest( 
     *           // Convert zipped triplet to string with '?' when optional is empty
     *           // Note: All arguments must be `std::optional`
     *           [](zip_opt<int> i, zip_opt<char> c, zip_opt<float> f) -> std::string { 
     *                   std::ostringstream result;
     *                   result << '[';
     *                   print_optional(result, i) << ", ";
     *                   print_optional(result, c) << ", ";
     *                   print_optional(result, f) << ']'; 
     *                   return result.str();
     *           }, 
     *           src::of_container("abcd"s), // String as source of 4 characters
     *           src::of_container(std::array{1.1f, 2.2f}) // Source of 2 floats
     *       )
     *       >>= apply::for_each([](const std::string& r) { std::cout << r << "\n"; });
     *  \endcode
     * This prints: \code
     * [1, a, 1.1]
     * [2, b, 2.2]
     * [3, c, ?]
     * [?, d, ?] \endcode
     */
    template <bool is_longest_sequence_zip, class R, class F, class ... Seqx>
    struct ZipSequence: public Sequence< R >
    {
        using base_t = Sequence< R >;
        using base_t::base_t;
        using element_t = typename base_t::element_t;

        constexpr ZipSequence(F applicator, Seqx&& ...sx) noexcept
            : _applicator(std::move(applicator))
            , _sources(std::move(sx)...)
            , _good(false)
        {
        }

        virtual void start() override
        {
            //start all
            size_t happy_cases = std::apply(
                [&](auto& ...seq) -> size_t {  
                    return (init_seq(details::get_reference(seq)) + ...);
            }, _sources);
            _good = false;
            if constexpr (is_longest_sequence_zip)
            {   //some sequences allowed be empty
                _good = happy_cases > 0;
            }
            else
                _good = (happy_cases == sizeof... (Seqx)); //all good when all sequences success
        }

        bool in_range() const override
        {
            return _good;
        }

        element_t current() const override
        {
            // this class as an applicator can be used with `void`, so add special case handler
            if constexpr(std::is_same_v<void, element_t>)
            {
                do_call(std::index_sequence_for<Seqx...>{});
            }
            else
            {
                return do_call(std::index_sequence_for<Seqx...>{});
            }
        }

        void next() override
        {
            _good = std::apply([&](auto& ...sequences) -> bool {
                bool stop = false;
                size_t happy_cases = (do_step(stop, details::get_reference(sequences)) + ...);
                if constexpr (is_longest_sequence_zip)
                {   //some sequences allowed be empty
                    return happy_cases > 0;
                }
                else
                    return (happy_cases == sizeof... (Seqx)); //all good when all sequences success

            }, _sources);
        }

    private:
        using all_seqeunces_t = std::tuple<Seqx...>;


        template <class T>
        static size_t init_seq(T& seq)
        {
            seq.start();
            return seq.in_range() ? 1 : 0;
        }

        template <class TSeq>
        static size_t do_step(bool& stop, TSeq& seq)
        {
            if(stop || !seq.in_range())
            {
                if constexpr (!is_longest_sequence_zip) //stop on shortest sequence
                    stop = true;
                return 0;
            }
            seq.next();    
            if( seq.in_range() )
                return 1; //at least 1 sequence succeeded
            if constexpr (!is_longest_sequence_zip)
                stop = true; //force other sequences not to step next and fail immediately
            return 0;
        }

        template <size_t... I>
        auto do_call(std::index_sequence<I...>) const
        {
            if constexpr( is_longest_sequence_zip ) //wrap all arguments with zip_opt
            {
                auto step = [](const auto& sequence) -> auto {
                    using result_t = zip_opt<decltype(sequence.current())>;
                    if(sequence.in_range())
                        return result_t(sequence.current());
                    else
                        return result_t{};
                    };

                return _applicator( 
                    step(details::get_reference(std::get<I>(_sources))) ...);
                
            }
            else
                return _applicator(
                    details::get_reference(std::get<I>(_sources)).current()...);
        }

        F _applicator;
        all_seqeunces_t _sources;
        bool _good;
    };


    template <bool is_longest_sequence_zip, class F, class ... Tx>
    struct ZipFactory : FactoryBase
    {
        using factories_t = decltype(std::make_tuple(std::declval<Tx>()...));

        template <class FLike, class ... Ux>
        constexpr ZipFactory(FLike&& f, Ux&& ... factories) noexcept
            : _factories(std::make_tuple(std::forward<Ux>(factories)... )) //to force removing explicit references
            , _applicator(std::forward<FLike>(f))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            return 
                std::apply(
                    [&](const auto& ... factory){
                        return construct_sequence(
                            _applicator, 
                            std::move(src),
                            details::get_reference(factory).compound() ...);
                    }, _factories);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            return 
                std::apply(
                    [&](auto&& ... factory){
                        return construct_sequence(
                            std::move(_applicator), 
                            std::move(src),
                            //`move` used only to cast to T&&
                            std::move(details::get_reference(factory)).compound() ...);
                    }, std::move(_factories));
        }

        // factory can be used as a source (without previous `sequence >> zip`)
        constexpr auto compound() const& noexcept
        {
            return
                std::apply(
                    [&](const auto& ... factory) {
                        return construct_sequence(
                            _applicator,
                            details::get_reference(factory).compound() ...);
                    }, _factories);
        }

        constexpr auto compound() && noexcept
        {
            return
                std::apply(
                    [&](auto&& ... factory) {
                        return construct_sequence(
                            std::move(_applicator),
                            //`move` used only to cast to T&&
                            std::move(details::get_reference(factory)).compound() ...);
                    }, std::move(_factories));
        }

    private:
        using applicator_t = std::decay_t<F>;

        template <class TApplicator, class ... Seqx>
        static constexpr auto construct_sequence(TApplicator&& applicator, Seqx&& ... sx) noexcept
        {
            if constexpr (is_longest_sequence_zip)
            {
                using result_t = 
                    decltype(
                        applicator(details::zip_t<Seqx> {}...)
                        );

                using sequence_t = ZipSequence< is_longest_sequence_zip,
                    result_t, applicator_t, std::decay_t<Seqx>...>;
                return sequence_t{
                    std::forward<TApplicator>(applicator),
                    std::move(sx)... //always move bcs sequence was just created
                };
            }
            else 
            {
                using result_t = 
                    decltype(applicator(details::get_reference(sx).current()...));

                using sequence_t = ZipSequence< is_longest_sequence_zip,
                    result_t, applicator_t, std::decay_t<Seqx>...>;
                return sequence_t{
                    std::forward<TApplicator>(applicator),
                    std::move(sx)... //always move bcs sequence was just created
                };
            }
        }

        factories_t _factories;
        applicator_t _applicator;
    };

} //ns:OP::flur

#endif //_OP_FLUR_CARTESIAN__H_
