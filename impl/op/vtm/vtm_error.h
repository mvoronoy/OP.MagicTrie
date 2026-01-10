#pragma once

#ifndef _OP_VTM_ERROR__H_
#define _OP_VTM_ERROR__H_

#include <op/common/Exceptions.h>

namespace OP::vtm
{
    struct ErrorCodes
    {
        enum
        {
            category = OP::ec_category * 1,

            // +1
            er_no_memory,
            // +2
            er_invalid_block,
            // +3
            er_transaction_not_started,
            // +4
            er_transaction_concurrent_lock,
            // +5
            er_transaction_ghost_state,
            // +6
            er_ro_transaction_started,
            // +7 
            er_cannot_start_ro_transaction,

            // +8
            er_file_open,
            // +9 
            er_file_already_exists,
            // +10
            er_invalid_signature,
            // +11
            er_write_file,
            // +12
            er_read_file,
            // +13
            er_memory_mapping
        };
    private:
        static inline std::string er_no_memory_str = "VTM: no (disk) memory";
        static inline std::string er_invalid_block_str = "VTM: invalid memory block";
        static inline std::string er_transaction_not_started_str = "VTM: transaction is not started";
        static inline std::string er_transaction_concurrent_lock_str = "VTM: concurrent transaction already lock this memory block";
        static inline std::string er_transaction_ghost_state_str = "VTM: invalid transaction in ghost state";
        static inline std::string er_ro_transaction_started_str = "VTM: cannot start transaction because read-only transaction is already started";
        static inline std::string er_cannot_start_ro_transaction_str = "VTM: cannot start read-only transaction";
        static inline std::string er_file_open_str = "VTM: file opening error";
        static inline std::string er_file_already_exists_str = "VTM: file already exists str";
        static inline std::string er_invalid_signature_str = "VTM: invalid signature";
        static inline std::string er_write_file_str = "VTM: file writing error";
        static inline std::string er_read_file_str = "VTM: file read error";
        static inline std::string er_memory_mapping_str = "VTM: file error during memory mapping";

        static std::string dispatch_error_code(unsigned error_code) noexcept
        {
            #define VTM_ERROR2_STR(code) case code: return code ## _str;
            switch(error_code)
            {
            VTM_ERROR2_STR(er_no_memory)
            VTM_ERROR2_STR(er_invalid_block)
            VTM_ERROR2_STR(er_transaction_not_started)
            VTM_ERROR2_STR(er_transaction_concurrent_lock)
            VTM_ERROR2_STR(er_transaction_ghost_state)
            VTM_ERROR2_STR(er_ro_transaction_started)
            VTM_ERROR2_STR(er_cannot_start_ro_transaction)
            VTM_ERROR2_STR(er_file_open)
            VTM_ERROR2_STR(er_file_already_exists)
            VTM_ERROR2_STR(er_invalid_signature)
            VTM_ERROR2_STR(er_write_file)
            VTM_ERROR2_STR(er_read_file)
            VTM_ERROR2_STR(er_memory_mapping)
            default:
                return "";
            };
            #undef VTM_ERROR2_STR
        }

        static inline bool dummy_init = ErrorDecoderRegistry::instance().register_error_category(
            category, &ErrorCodes::dispatch_error_code);
    };
} //ns:OP::vtm
#endif // _OP_VTM_ERROR__H_