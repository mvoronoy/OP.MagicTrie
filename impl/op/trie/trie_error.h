#pragma once

#ifndef _OP_TR_ERROR__H
#define _OP_TR_ERROR__H

#include <op/common/Exceptions.h>

namespace OP::trie
{
    struct ErrorCodes
    {
        enum
        {
            category = OP::ec_category * 2,

            // +1
            er_file_open,
            // +2
            er_write_file,
            // +3
            er_read_file,
            // +4
            er_invalid_signature,
            // +5
            er_file_already_exists,
            // +6
            er_memory_mapping
        };
    private:

        static inline std::string er_file_open_str = "Trie: file opening error";
        static inline std::string er_write_file_str = "Trie: write file error";
        static inline std::string er_read_file_str = "Trie: read file error";
        static inline std::string er_invalid_signature_str = "Trie: invalid signature";
        static inline std::string er_file_already_exists_str = "Trie: file already exists";
        static inline std::string er_memory_mapping_str = "Trie: memory mapping error";

        static std::string dispatch_error_code(unsigned error_code) noexcept
        {
             #define TRIE_ERROR2_STR(code) case code: return code ## _str;
             switch(error_code)
             {
                TRIE_ERROR2_STR(er_file_open)
                // +2
                TRIE_ERROR2_STR(er_write_file)
                // +3
                TRIE_ERROR2_STR(er_read_file)
                // +4
                TRIE_ERROR2_STR(er_invalid_signature)
                // +5
                TRIE_ERROR2_STR(er_file_already_exists)
                // +6
                TRIE_ERROR2_STR(er_memory_mapping)
                default:
                    return "";
             };
             #undef TRIE_ERROR2_STR
        }
        static inline bool dummy_init = ErrorDecoderRegistry::instance().register_error_category(
            category, &ErrorCodes::dispatch_error_code);

    };
} //ns:OP::trie

#endif //_OP_TR_ERROR__H
