#ifndef _OP_TRIE_UNSIGNED__H_
#define _OP_TRIE_UNSIGNED__H_

#include <type_traits>

namespace OP
{
    namespace trie
    {
        /**
        *   Allows avoid undefined behaviour while unsigned type increment. On overflow make value 0.
        * @return new 
        */
        template <class U>
        inline U uinc(U &v)
        {
            static_assert(std::is_unsigned<U>::value, "type must be unsigned");
            return std::numeric_limits<U>::max() == v ? (v = 0) : ++v; 
        }

        template <class U>
        inline U uadd(U v, U delta)
        {
            static_assert(std::is_unsigned<U>::value, "type must be unsigned");
            if( delta > (std::numeric_limits<U>::max() - v) )
            {
                return delta - 1;        
            }
            return (v + delta); 
        }
    } //trie
} //OP
#endif //_OP_TRIE_UNSIGNED__H_
