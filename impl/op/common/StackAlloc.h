#pragma once
#ifndef _OP_COMMON_STACKALLOC__H_
#define _OP_COMMON_STACKALLOC__H_
#include <cstdint>
#include <stdexcept>

namespace OP
{
    template <typename T>
    struct MemBuf 
    {
        using this_t = MemBuf<T>;
        
        constexpr MemBuf()
        {
        }

        template <class ... Ux>
        constexpr MemBuf(Ux&& ... ux)
        {
            ::new(_data) T(std::forward<Ux>(ux)...);
            _init = true;
        }

        constexpr MemBuf(const this_t& other)
        {
            if(other._init)
            {
                ::new(_data) T(*other.data());
                _init = true;
            }
            else
                _init = false;
        }
        
        constexpr MemBuf(this_t&& other) noexcept
        {
            if(other._init)
            {
                ::new(_data) T(std::move(*other.data()));
                _init = true;
            }
            else
                _init = false;
        }
        
        ~MemBuf()
        {
            destroy();
        }

        constexpr operator bool() const
        {
            return _init;
        }
        
        constexpr bool operator !() const
        {
            return !_init;
        }

        T& operator *() 
        {
            if(!_init)
                throw std::runtime_error("Use unitialized object");
            return *data();
        }
        
        const T& operator *() const
        {
            if(!_init)
                throw std::runtime_error("Use unitialized object");
            return *data();
        }
        
        T* operator ->()
        {
            return &operator *();
        }
        const T* operator ->() const
        {
            return &operator *();
        }

        this_t& operator = (const T& t)
        {
            destroy();
            ::new(_data) T(t);
            _init = true;
            return *this;
        }

        this_t& operator = (T&& t) noexcept
        {
            destroy();
            ::new(_data) T(std::move(t));
            return *this;
        }
        template <class ... Ux>
        this_t& construct(Ux&& ... ux)
        {
            destroy();
            ::new(_data) T(std::forward<Ux>(ux)...);
            _init = true;
            return *this;
        }

        T* data()
        {
             return std::launder(reinterpret_cast<T*>(_data));
        }

        const T* data() const
        {
             return std::launder(reinterpret_cast<T*>(_data));
        }

        void destroy()
        {
            if( _init )
            {
                std::destroy_at(data());
                _init = false;
            }
        }

    private:

        bool _init = false;
        alignas(T) std::byte _data[sizeof(T)];
    };
    
    /** Reserv some memory that can accomodate all Tx ... types than provide
    * access to one of the instance by interface specified as TInterface.
    * Note all Tx must support the TInterface interface.
    */
    template <class TInterface, class ... Tx>
    struct Multiimplementation
    {
        static_assert( std::conjunction_v<std::is_base_of<TInterface, Tx>...>, 
            "All Tx... must inherit base interface TInterface");
        using this_t = Multiimplementation<TInterface, Tx...>;
        
        constexpr Multiimplementation() noexcept {}
        
        Multiimplementation(const Multiimplementation&) = delete;
        Multiimplementation(Multiimplementation&&) = delete;

        ~Multiimplementation()
        {
            destroy();
        }
        
        template <class U, class ...Args>
        TInterface* construct(Args&& ...arg)
        {
            if( _init )
                throw std::runtime_error("Instance is already initialized, destroy it first");
            static_assert(
                 std::disjunction_v<std::is_same<U, Tx>...>, 
                 "Try to construct type U not defined in Tx... list");
            TInterface *result = ::new(_data) U(std::forward<Args>(arg)...);
            _init = true;
            return result;
        }

        TInterface* operator ->() 
        {
             return get();
        }

        TInterface* get() 
        {
            if(!_init)
                throw std::runtime_error("Instance is not initialized");
            return data();
        }
        TInterface& operator *()
        {
            return *get();
        }
        void destroy()
        {
            if( _init )
            {
                std::destroy_at(data());
                _init = false;
            }
        }
    private:
        bool _init = false;
        TInterface* data()
        {
            return std::launder(reinterpret_cast<TInterface*>(_data));
        }
        static constexpr size_t bigest_align_c = std::max( {alignof(TInterface), alignof(Tx)...} );
        static constexpr size_t bigest_sizeof_c = std::max( {sizeof(TInterface), sizeof(Tx)...} );

        alignas(bigest_align_c) std::byte _data[bigest_sizeof_c];
        
    };

}//ns:contata5::common
#endif //_OP_COMMON_STACKALLOC__H_