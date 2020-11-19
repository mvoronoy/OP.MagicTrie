#ifndef _OP_TR_EXCEPTIONS__H
#define _OP_TR_EXCEPTIONS__H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <exception>
#include <string>

namespace OP
{
    namespace trie
    {
        enum ErrorCode
        {
            no_error = 0,
            //
            er_file_open = 1,
            er_write_file = 2,
            er_read_file = 3,
            er_invalid_signature = 4,
            er_memory_mapping = 10,
            //
            //memory exceptions:
            //
            er_memory_need_compression = 20,
            er_no_memory = 21,
            /**memory management function got invalid pointer */
            er_invalid_block = 22,
            //
            //  Named map exception
            //
            /**Named map is not specified at SegmentManager creation time*/
            er_no_named_map = 30,
            /**As key use up to 8 symbols string*/
            er_named_map_key_too_long = 31,
            er_named_map_key_exists = 32,
            //
            //  Transactions
            //
            er_transaction_not_started = 40,
            /**Cannot obtain lock over already existing*/
            er_transaction_concurent_lock = 41,
            /** Using already closed transaction (transaction in ghost-state)*/
            er_transaction_ghost_state = 42,
            /**Transactional memory blocks cannot be overlapped. Blocks may be nested, adjasted or separated. */
            er_overlapping_block = 43
        };
        struct Exception : public std::logic_error
        {
            explicit Exception(ErrorCode code):
                _code(code),
                std::logic_error(""){}

            Exception(ErrorCode code, const char * error):
                _code(code),
                std::logic_error(error){}
            operator ErrorCode () const
            {
                return _code;
            }
            ErrorCode code() const
            {
                return _code;
            }

        private: 
            ErrorCode _code;
        };
        struct TransactionException : public Exception
        {
            explicit TransactionException(ErrorCode code):
                Exception(code){}

        };
        struct TransactionIsNotStarted : public TransactionException
        {
            TransactionIsNotStarted() :
                TransactionException(er_transaction_not_started){}
        };
    }  //end of namespace trie
} //end of namespace OP


#endif //_OP_TR_EXCEPTIONS__H
