#pragma once

#ifndef _OP_TRIE_SERVANTS_SERVICEUID__H_
#define _OP_TRIE_SERVANTS_SERVICEUID__H_


namespace OP::trie::servants
{
    struct ServiceUID 
    {
        enum
        {
            trie_header = 0,
            space_pool_service = 1,
            alloc_service = 2,
            _last_
        };
    };
} //ns:OP::trie::servants

#endif //_OP_TRIE_SERVANTS_SERVICEUID__H_