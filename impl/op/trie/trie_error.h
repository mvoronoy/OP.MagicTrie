#pragma once

#ifndef _OP_TR_ERROR__H
#define _OP_TR_ERROR__H

#include <op/common/Exceptions.h>

namespace OP::trie
{
    enum ErrorCodes
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
} //ns:OP::trie

#endif //_OP_TR_ERROR__H
