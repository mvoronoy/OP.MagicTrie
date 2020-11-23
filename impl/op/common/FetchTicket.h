#ifndef _OP_UTILS_FETCHTICKET__H_
#define _OP_UTILS_FETCHTICKET__H_
#include <atomic>
namespace OP{
namespace utils{

        template <class I>
        struct FetchTicket
        {
            FetchTicket():
                _ticket_number(0),
                _turn(0)
            {
            }

            void lock()
            {
                size_t r_turn = _ticket_number.fetch_add(1);
                while (!_turn.compare_exchange_weak(r_turn, r_turn))
                    /*empty body std::this_thread::yield()*/;
            }
            void unlock()
            {
                _turn.fetch_add(1);
            }

            std::atomic<I> _ticket_number;
            std::atomic<I> _turn;

        };
        //////////////////////////////////
        /**
        *   Wrapper to grant RAI operation by invoking pair of methods
        */
        template < class T, 
            void (T::*start_op)(), 
            void (T::*end_op)() >
        struct operation_guard_t
        {
            operation_guard_t(T& ref) :
                _ref(&ref),
                _is_closed(false)
            {
                (_ref->*start_op)();
            }
            void close()
            {
                if (!_is_closed)
                    (_ref->*end_op)();
                _is_closed = true;
            }
            ~operation_guard_t()
            {
                close();
            }
        private:
            T* _ref;
            bool _is_closed;
        };

    
} //ns:utils
}//ns:OP
#endif //_OP_UTILS_FETCHTICKET__H_
