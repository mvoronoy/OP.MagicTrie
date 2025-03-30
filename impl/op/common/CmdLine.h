#ifndef _OP_COMMON_CMDLINE__H_
#define _OP_COMMON_CMDLINE__H_

#include <algorithm>
#include <optional>
#include <functional>
#include <string>
#include <sstream>

#include <op/common/has_member_def.h>
#include <op/common/ftraits.h>

namespace OP::console
{
    namespace details {
        
        template <class TArg, class Iter>
        auto take_arg(Iter& from, Iter& to)
        {
            if (from == to)
                throw std::invalid_argument("not enough arguments");
            using arg_t = std::decay_t<TArg>;
            if constexpr (std::is_same_v<std::string, arg_t>)
            {//no conversion needed
                return *from++;
            }
            else
            {
                arg_t a;
                std::istringstream(*from++) >> a;
                return a;
            }
        };

        template <class F, class Iter, size_t...I, typename ftraits_t = OP::utils::function_traits<std::decay_t<F>>>
        void invoke(F&& f, Iter& from, Iter to, std::index_sequence<I...>)
        {
            f(take_arg< typename ftraits_t::template arg_i<I> >(from, to) ...);
        }

    }//ns:details
    using arg_value_t = std::optional<std::string>;
    using namespace std::string_literals;

    /**
    * Ingredient to OP::console::Arg declaration to define mandatory argument. Usage:
    *   \code
    *   CommandLineParser clp(
    *       arg(key("--test"), required(), assign(&x.v))
    *   ); \endcode
    * 
    * States that `--test` must present during parsing
    * When mandatory argument missed std::invalid_argument exception is raised
    */
    struct required
    {
        bool _assigned = false;

        template <class Iter>
        void when_match(Iter&, Iter) noexcept
        {
            _assigned = true;
        }
        
        void post_process()
        {
            if (!_assigned)
                throw std::invalid_argument("argument is required"s);
        }
    };

    /**
    * Ingredient to OP::console::Arg declaration to define human readable description of  
    * a command. Usage:  \code
    *       CommandLineParser cml(
    *    arg(key("--test"), desc("define test argument"), assign(&x.v))
    *   ); \endcode
    * 
    * Together with CommandLineParser#usage allows print explanation of the specific key
    */
    struct desc
    {
        template <class T>
        explicit desc(T&& t)
            : _str(std::forward<T>(t)) {}

        const std::string& info() const noexcept
        {
            return _str;
        }

        std::string _str;
    };

    /**
    * Ingredient to OP::console::Arg declaration to declare matching key. You can specify
    *  more than one `key` for the single parameter.
    * Usage:  \code
    *       CommandLineParser clp(
    *    arg(key("-t"), key("--test"), desc("define test argument"), assign(&x.v))
    *   ); \endcode
    * 
    * States that `--test` or `-t` assigns variable value `&x.v`.
    */
    struct key
    {
        template <class T>
        explicit key(T&& t) noexcept
            : _str(std::forward<T>(t)) {}

        template <class Iter>
        bool match(Iter& current, Iter) noexcept
        {
            if (_str == *current)
            {
                ++current;
                return true;
            }
            return false;
        }

        const std::string& id() const noexcept
        {
            return _str;
        }

        std::string _str;
    };

    /**
    * Ingredient to OP::console::Arg declaration to declare handler of free (stroll) parameter
    * without `key.
    * Usage:  \code
    *       CommandLineParser clp(
    *    arg( desc("define argument without a key"), stroll{}, assign(&x.v))
    *   ); \endcode
    * 
    * When no other key recognized assign variable value `&x.v`.
    */
    struct stroll
    {
        stroll() = default;

        template <class Iter>
        bool match_fallback(Iter&, Iter) noexcept
        {
            return true;
        }

        const std::string& id() const
        {
            const static std::string id("free argument");
            return id;
        }

        template <class Iter>
        void when_match(Iter& current, Iter) noexcept
        {
            ++current; //need if no `assign` operation followed
        }
    };

    /**
    * Ingredient to OP::console::Arg declaration to declare callback
    * when argument matches.
    * Usage:  \code
    *       CommandLineParser clp(
    *    arg( key("-t"), 'action([](auto& value){ std::cout << value << "\n";}))
    *   ); \endcode
    * 
    * For `-t` key prints associated value.
    */
    template <class F>
    struct action
    {
        explicit action(F f) noexcept
            : _fun(std::move(f))
        {}

        template <class Iter>
        void when_match(Iter& current, Iter end)
        {
            using ftraits_t = OP::utils::function_traits<F>;
            details::invoke(
                _fun, current, end, std::make_index_sequence<ftraits_t::arity_c >{});
        }
    private:
        F _fun;
    };

    /**
    * Ingredient to OP::console::Arg declaration to assign value 
    * when argument matches.
    * Usage:  \code
    *       CommandLineParser cml(
    *    arg(key("-t"), key("--test"), desc("define test argument"), assign(&x.v))
    *   ); \endcode
    * 
    * States that `--test` or `-t` assigns variable value `&x.v`.
    * This class has specialization for 'bool' type allowing parse keys without values:
    * Usage:  \code
    *   bool is_set = false;
    *   CommandLineParser cml(
    *       arg(key("--bool"), desc("define boolean checker"), assign(&is_set))
    *   ); \endcode
    *
    * Code will succeed with `--bool` 
    */
    template <class T>
    struct assign
    {
        explicit assign(T* dest) noexcept
            : _dest(dest) {}

        template <class Iter>
        void when_match(Iter& current, Iter end)
        {
            details::invoke(
                [this](T t) { *_dest = std::move(t); }, 
                current, end, std::make_index_sequence<1>{});
        }

    private:
        T* _dest;
    };

    template <>
    struct assign<bool>
    {
        explicit assign(bool* dest) noexcept
            : _dest(dest) {}
        
        template <class Iter>
        void when_match(Iter&, Iter) noexcept
        {
            *_dest = true;
        }

    private:
        bool* _dest;
    };

    /** Declare rules for single parameters. For possible usage see
    * #key, #assign, #stroll, #action, #desc, #required
    */
    template <class ... Tx>
    struct Arg 
    {
        explicit Arg(Tx&& ...tx) noexcept
            : _entries(std::forward<Tx>(tx)...)
        {}

        /** Tries match current parsing argument with key */
        template <class Iter>
        bool match(Iter& current, Iter end)
        {
            auto single_match = [&](auto& zhis) {
                using zhis_t = std::decay_t<decltype(zhis)>;
                if constexpr (has_match<zhis_t, Iter>::value) { return zhis.match(current, end); }
                else { return false; }
            };
            return std::apply([&](auto& ... ent) {
                return (single_match(ent) || ...); }, _entries);
        }

        /** What to do when no matching argument found */
        template <class Iter>
        bool match_fallback(Iter& current, Iter end)
        {
            auto single_fallback = [&](auto& zhis) {
                using zhis_t = std::decay_t<decltype(zhis)>;
                if constexpr (has_match_fallback<zhis_t, Iter>::value)
                {
                    return zhis.match_fallback(current, end);
                }
                else { return false; }
            };

            return std::apply([&](auto& ... ent) {
                return (single_fallback(ent) || ...); }, _entries);
        }

        /** Action taken when argument matches to current parsed key. 
        * Multiple consumers are allowed
        */
        template <class Iter>
        void when_match(Iter& current_arg, Iter end)
        {
            size_t take_out = 0;
            auto single_on = [&](auto& zhis) {
                using zhis_t = std::decay_t<decltype(zhis)>;
                if constexpr (has_when_match<zhis_t, Iter>::value)
                {
                    auto temp_i = current_arg;
                    zhis.when_match(temp_i, end);
                    size_t taken = temp_i - current_arg;
                    take_out = std::max(take_out, taken);
                }
            };
            try {
                std::apply([&](auto& ... ent) {
                    (single_on(ent), ...); }, _entries);
            }
            catch (const std::exception& ex)
            {
                enrich_exception(ex);
                throw; //re-raise without upgrade
            }
            std::advance(current_arg, take_out);
        }

        /** When entire command line is parsed invokes final check (see #required that applied aftermath) */
        void post_process()
        {
            auto single_step = [](auto& zhis) {
                using zhis_t = std::decay_t<decltype(zhis)>;
                if constexpr (has_post_process<zhis_t>::value)
                {
                    zhis.post_process();
                }
            };
            try
            {
                std::apply([&](auto& ... ent) {
                    (single_step(ent), ...);
                    }, _entries);
            }
            catch (const std::exception& ex)
            {
                enrich_exception(ex);
                throw; //re-raise without upgrade
            }
        }

        /** Renders human readable identifier of the current argument */
        std::string identify(const std::string& separator = ", "s)
        {
            std::string result, current;
            auto step = [&](auto& m) {
                using arg_t = std::decay_t<decltype(m)>;
                if (!_id_of(m, current))
                    return;
                if (!result.empty())
                    result += separator;
                result += current;
            };
            // try improve message by adding `id` information
            std::apply([&](auto& ...ent) {(step(ent), ...); }, _entries);
            return result;
        }

        /** Renders human readable description of the current argument */
        std::string usage(const std::string& separator, const std::string& pad)
        {
            std::string result, current;
            auto step = [&](auto& m) {
                using arg_t = std::decay_t<decltype(m)>;
                if constexpr (has_info<arg_t>::value)
                {
                    current += pad;
                    current += m.info();
                }
            };
            // try improve message by adding `id` information
            std::apply([&](auto& ... arg) {(step(arg), ...); }, _entries);
            result += identify(separator);
            result += current;
            return result;
        }

    private:
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(match)
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(match_fallback)
        OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(when_match)
        OP_DECLARE_CLASS_HAS_MEMBER(post_process)
        OP_DECLARE_CLASS_HAS_MEMBER(info)
        OP_DECLARE_CLASS_HAS_MEMBER(id)

        void enrich_exception(const std::exception& ex)
        {
            std::string id;
            // try improve message by adding `id` information
            if (std::apply([&](auto& ... ent) {
                return (_id_of(ent, id) || ...);
                }, _entries))
            {
                throw std::invalid_argument("Argument error:'"s + id + "': "s + ex.what());
            }
            
        }

        template <class T>
        bool _id_of(T& t, std::string& id_buf)
        {
            if constexpr (has_id<T>::value)
            {
                id_buf = t.id();
                return true;
            }
            return false;
        }

        std::tuple<Tx...> _entries;
    };

    template <class ...Tx>
    auto arg(Tx&& ... tx)
    {
        return Arg(std::forward<Tx>(tx)...);
    }

    /** Parser of command line. For possible usage see
    * #key, #assign, #stroll, #action, #desc, #required.
    *\tparam TArg definition constructed from multiple #arg definitions
    */
    template <class ...TArg>
    struct CommandLineParser
    {
        explicit CommandLineParser(TArg&& ...rules) noexcept
            : _rules(std::make_tuple(std::forward<TArg>(rules)...))
        {
        }
        /** Public configurable value used in shaping usage information to
        * separate key information from description
        */
        std::string usage_pad = ": ";
        /** Public configurable value used in shaping usage information to
        * separate multiple keys
        */
        std::string usage_separator = ", ";
        /** Public configurable value used in shaping usage information to
        * each argument description
        */
        std::string usage_line_feed = "\n";

        /** Renders usage text from the existing defined rules */
        void usage(std::ostream& os) 
        {
            size_t n = 0;
            std::apply([&](auto& ... a)
                {   
                    ((os << (n++ ? usage_line_feed : ""s) << a.usage(usage_separator, usage_pad)), ...);
                }, _rules);
            os << (n ? usage_line_feed : ""s);
        }
        
        /** Parse multiple argument from template argument `Container`.
        * Note that method is not idempotent (changes inner state)
        * \tparam Container - any stl container like std::vector, std::initializer_list ...
        */
        template <
            typename Container,
            typename = std::enable_if_t<std::is_same_v<typename Container::value_type, std::string>>
        >
        void parse(Container args)
        {
            auto cend = args.end();
            for (auto i = args.begin(); i != cend; /*++i*/)
            {
                bool is_fallback = false;
                auto match_and_run = [&](auto& m) {
                    //if Arg someway matched to the current key m invoke handler `_on` and stop iteration
                    bool succ = is_fallback ? m.match_fallback(i, cend) : m.match(i, cend);
                    if (succ)
                    {
                        m.when_match(i, cend);
                    }
                    return succ;
                };

                auto match_res = std::apply([&](auto& ... m) { //check if some arg matched to current key
                    return (match_and_run(m) || ...);
                    }, _rules);

                if (!match_res)
                {//check if fallback applicable
                    is_fallback = true;
                    match_res = std::apply([&](auto& ... m) {
                        return (match_and_run(m) || ...); }, _rules);
                }
                if (!match_res)
                {
                    throw std::invalid_argument(std::string("Unknown argument: '") + *i + "'"s);
                }
            }
            std::apply([](auto & ...arg) {
                (arg.post_process(), ...);
                }, _rules);
        }

        /** Parse command line passed from `int main(int argc, char **argv)` syntax
        * Note that this function ignores `argv[0]` since it reserved for program executable name
        */
        void parse(int argc, const char**argv)
        {
            if (argc < 1)
                throw std::runtime_error("Wrong number of parser arguments provided, it must be at least 1.");
            std::vector<std::string> cmds(argv + 1, argv + argc);
            parse(std::move(cmds));
        }

    private:
        std::tuple<TArg...> _rules;
    };

}//OP::console

#endif //_OP_COMMON_CMDLINE__H_
