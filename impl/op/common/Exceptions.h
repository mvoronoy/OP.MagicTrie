#ifndef _OP_TR_EXCEPTIONS__H
#define _OP_TR_EXCEPTIONS__H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <exception>
#include <string>
#include <sstream>
#include <shared_mutex>
#include <unordered_map>
#include <functional>

namespace OP
{
    enum ErrorCode
    {
        ec_no_error = 0,
        ec_category = 0x1000
    };

    /**
    * \brief Provide the extendable by developer bridge between error codes and human readable error explanation.
    *
    *  ErrorCode logically splits into an error category `(developer provided as ec_category  * my_category)` and 
    *  a specific error value (in range 1..0xfff). The class ErrorDecoderRegistry allows register callback with category 
    *  to discover error explanation string.
    *  From development perspective this class is a singleton.
    *
    */
    struct ErrorDecoderRegistry
    {
        static ErrorDecoderRegistry& instance() noexcept
        {
            static ErrorDecoderRegistry _instance = {};
            return _instance;
        }
    
        template <class F>
        [[maybe_ignored]] bool register_error_category(unsigned category, F callback)
        {
            std::unique_lock guard(_registered_categories_acc);
            auto [prev, succ] = _registered_categories.try_emplace(category, callback_t(std::move(callback)));
            if(!succ)
            {
                std::ostringstream os;
                os << "Error category 0x" << std::hex << category << " is already registered";
                throw std::runtime_error(os.str().c_str());
            }
            return true;
        }

        template <class ...Tx>
        std::string format_error(unsigned error_code, const Tx&...extra_args) const
        {
            //extract error category
            const unsigned category = error_code / ec_category;
            std::ostringstream os;

            std::ios_base::fmtflags old_flags( os.flags() );
            os << "(0x" << std::hex << std::setw(8) << std::setfill('0') << error_code << ") ";
            os.flags(old_flags);

            std::shared_lock ro_guard(_registered_categories_acc);
            auto found = _registered_categories.find(category);
            if( found != _registered_categories.end() )
                os << found->second(error_code) << ".";
            ro_guard.unlock();

            // optional additional args
            ((os << extra_args), ...);

            return os.str();
        }
    private:
        ErrorDecoderRegistry() = default;
        ErrorDecoderRegistry(const ErrorDecoderRegistry&) = delete;
        ErrorDecoderRegistry(ErrorDecoderRegistry&&) = delete;

        using callback_t = std::function<std::string(unsigned)>;

        std::unordered_map<unsigned, callback_t> _registered_categories;
        mutable std::shared_mutex _registered_categories_acc;
    };

    /**
    * \brief General exception type for the entire OP library.
    *
    * This exception inherits from `std::logic_error` to allow seamless integration
    * with standard C++ error-handling mechanisms.
    *
    * Each Exception instance carries an error code that is logically split into
    * an error category and a specific error value. Optionally, the error code can
    * be translated into a human-readable string using `ErrorDecoderRegistry`.
    */
    struct Exception : public std::exception
    {
        /**
         * \brief Construct an OP::Exception from an error code and optional explanatory arguments.
         *
         * \param code Constant error code. It is recommended to compose this value as
         *        `error_category * OP::ec_category + internal_error_code`. In this case,
         *        the implementation attempts to resolve the error category via a
         *        callback registered in `ErrorDecoderRegistry::register_error_category`
         *        and translate the code into a human-readable description.
         *
         * \param opt_args Optional additional arguments that are appended sequentially
         *        to the resulting exception message.
         */
        template <class ... Tx>
        explicit Exception(unsigned code, const Tx& ...opt_args)
            : _code(code)
            , _error_text(ErrorDecoderRegistry::instance().format_error(code, opt_args...))
        {
        }

        unsigned code() const
        {
            return _code;
        }

        virtual const char* what() const noexcept override 
        {
            return _error_text.c_str();
        }

    private: 
        unsigned _code;
        std::string _error_text;
    };

} //ns:OP


#endif //_OP_TR_EXCEPTIONS__H
