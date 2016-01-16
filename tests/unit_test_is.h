#ifndef _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
#define _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1

#include <string>
#include <typeinfo>
#include <functional>
#include <map>
#include <set>

namespace OP
{
    namespace utest{
        namespace is{

            template <class T1, class T2>
            inline std::function< bool() > equals(
                T1 && t1, 
                T2 && t2)
            {
                return [=](){
                    return t1 == t2;
                };
            }
            inline std::function< bool() > equals(
                const char* t1, 
                const char* t2)
            {
                return [t1, t2](){
                    return strcmp(t1, t2) == 0;
                };
            }
            inline std::function< bool() > equals(
                const std::uint8_t* t1, 
                const std::uint8_t* t2,
                size_t size)
            {
                auto f = [t1, t2, size](){
                    return memcmp(t1, t2, size) == 0;
                };
                return f;
            }
            template <class T1, class T2>
            inline std::function< bool() > less(
                const T1& t1, 
                const T2& t2)
            {
                return [t1, t2](){
                    return t1 < t2;
                };
            }

            inline std::function< bool() > less(
                const char* t1, 
                const char* t2)
            {
                return [t1, t2](){
                    return strcmp(t1, t2) < 0;
                };
            }
        }
        
    } //utest
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
