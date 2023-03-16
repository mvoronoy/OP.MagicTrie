#ifndef _OP_UTEST_DETAILS__H_
#define _OP_UTEST_DETAILS__H_

#include <sstream>

namespace OP::utest
{
    /**
     * ostream-like class that allows:
     * -# inline construction. For example:
     *   OP::utils::Details() << 123 << "abc";
    */
    struct Details
    {
        Details() = default;

        Details(Details&& other) noexcept
            : _result(std::move(other._result))
        {
        }

        Details(const Details& other)
            : _result(other._result.str())
        {
//            operator<<(as_stream(), other);
        }

        std::string result() const
        {
            return _result.str();
        }

        bool is_empty()
        {
            _result.seekp(0, std::ios_base::end);
            auto offset = _result.tellp();
            return offset == (std::streamoff)0;
        }

        template <class T>
        friend inline Details& operator << (Details& d, const T& t)
        {
            d._result << t;
            return d;
        }

        template <class T>
        friend inline Details operator << (Details&& d, const T& t)
        {
            Details inl(std::move(d));
            inl._result << t;
            return inl;
        }

        template <class Os>
        friend inline std::ostream& operator <<(Os& os, const Details& d)
        {
            os << d._result.str();  //rdbuf() not working there :(
            return os;
        }

    private:
        std::ostringstream _result;
    };


}//ns: OP::utest
#endif //
