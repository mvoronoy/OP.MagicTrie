#pragma once

#ifndef _OP_VTM_ERROR__H_
#define _OP_VTM_ERROR__H_

#include <op/Exception>

namespace OP::vtm
{
    enum ErrorCodes
    {
        category = OP::ec_category * 1,

        // +1
        er_no_memory,
        // +2
        er_invalid_block,
        // +3
        er_transaction_not_started,
        // +4
        er_transaction_concurent_lock,
        // +5
        er_overlapping_block,
        // +6
        er_ro_transaction_started,
        // +7 
        er_cannot_start_ro_transaction

    };
} //ns:OP::vtm
#endif // _OP_VTM_ERROR__H_