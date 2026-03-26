#pragma once
#ifndef _OP_COMMON_STACKALLOC__H_
#define _OP_COMMON_STACKALLOC__H_

#include <cstdint>
#include <stdexcept>
#include <variant>

namespace OP
{
    /** \brief error on using non-initialized object */
    class not_initialized_error : public std::runtime_error
    {
    public:
        not_initialized_error() 
            : std::runtime_error("using memory buffer with uninitialized object, construct it first.")
        {}
    };

    template <class T>
    struct MemoryAlignedStorage
    {
        constexpr T& operator *() noexcept
        {
            return *data();
        }

        constexpr const T& operator *() const noexcept 
        {
            return *data();
        }

        /** \brief Provide low-level access to occupied memory without additional 
        *   checks if object has been initialized. 
        */
        T* data() noexcept
        {
             return std::launder(reinterpret_cast<T*>(_data));
        }

        /** \brief Provide low-level constant access to occupied memory without 
        * additional checks if object has been initialized. 
        */
        const T* data() const noexcept
        {
             return std::launder(reinterpret_cast<const T*>(_data));
        }

        T* operator ->()
        {
            return data();
        }

        const T* operator ->() const
        {
            return data();
        }
    private:
        alignas(T) std::byte _data[sizeof(T)] = {};

    };

    /** \brief Supports the construction of objects in a memory buffer.
    *
    * Sometimes, it is important to create objects of a specific class without using heap memory. The 
    *   following list outlines some use cases for this feature:
    * - Time-critical scenarios: Using MemBuf on the stack avoids CRT heap allocation, reducing delays.
    * - Postponed object construction (similar to `std::optional`) for resource-constrained or greedy scenarios.
    * - When `std::optional` is not a case because of `const` constraints.
    * - When you have intensive cycles of constructing and destroying a single object, you can reuse the same 
    *   memory.
    * - When, for some reason, class has only constructors without ability to implement copy/move operators.
    *
    *   Internally, MemBuf contains a byte buffer big enough to accommodate the type specified by the template 
    *   parameter T, taking into account the current platform's alignment requirements.
    *
    *   \tparam T type of object to support.
    */
    template <typename T>
    struct MemBuf
    {
        using element_t = T;
        using this_t = MemBuf<T>;
        
        /** Create un-initialized in memory buffer (no heap operations is involved) */
        constexpr MemBuf() noexcept = default;

        /** Construct in-place object of type `T` with constructor arguments specifed by `Ux...` */
        template <class ... Ux>
        constexpr MemBuf(Ux&& ... ux) noexcept(std::is_nothrow_constructible_v<T, Ux...>)
        {
            ::new(_data.data()) T(std::forward<Ux>(ux)...);
            _init = true;
        }

        /** \brief Copy constructor.
        *   
        * When the source object is not initialized, the copy is also not initialized. However, 
        * when the original MemBuf contains a valid object, the copy constructor of `T` is used.
        */
        constexpr MemBuf(const MemBuf& other) noexcept(std::is_nothrow_constructible_v<T, const T&>)
        {
            if(other._init)
            {
                ::new(_data.data()) T(*other.data());
                _init = true;
            }
            else
                _init = false;
        }
        
        /** \brief Move constructor.
        *   
        * When the source object is not initialized, the copy is also not initialized. However, 
        * when the original MemBuf contains a valid object, the move constructor of `T` is used.
        */
        constexpr MemBuf(MemBuf&& other) noexcept(std::is_nothrow_constructible_v<T, T&&>)
        {
            if(other._init)
            {
                ::new(_data.data()) T(std::move(*other.data()));
                other._init = false;
                _init = true;
            }
            else
                _init = false;
        }

        /** Conditionally destroy contained object, if instance was initialized */
        ~MemBuf() noexcept(std::is_nothrow_destructible_v<T>)
        {
            destroy();
        }

        /** check if buffer contains initialized instance of `T` */
        constexpr bool has_value() const noexcept
        {
            return _init;
        }
        
        /** check if buffer contains initialized instance of `T` */
        constexpr operator bool() const noexcept
        {
            return has_value();
        }
        
        /** check if buffer does not contain initialized instance of `T` */
        constexpr bool operator !() const noexcept
        {
            return !has_value();
        }

        /** \brief take reference of contained object.
        *
        * \throws not_initialized_error (aka std::runtime_error) when instance is not initialized.
        */
        T& operator *()
        {
            if(!_init)
                throw not_initialized_error{};
            return *data();
        }
        
        /** \brief take const reference of contained object.
        *
        * \throws not_initialized_error (aka std::runtime_error) when instance is not initialized.
        */
        const T& operator *() const
        {
            if(!_init)
                throw not_initialized_error{};
            return *data();
        }
        
        /** \brief member access operator for non-const objects.
        *
        * \throws not_initialized_error (aka std::runtime_error) when instance is not initialized.
        */
        T* operator ->()
        {
            return &operator *();
        }

        /** \brief member access operator for const objects.
        *
        * \throws not_initialized_error (aka std::runtime_error) when instance is not initialized.
        */
        const T* operator ->() const
        {
            return &operator *();
        }

        /** \brief Assigns an instance from an existing instance of `T`.
        *
        * When this MemBuf is not initialized, it behaves like the copy 
        *   constructor of `T` and constructs a copy.
        * Otherwise, it delegates the call to the assignment operator of `T`.
        */
        this_t& operator = (const T& t)
            noexcept(std::is_nothrow_constructible_v<T, const T&> && std::is_nothrow_assignable_v<T, const T&>)    
        {
            if( _init )
            {
                *data() = t;
            }
            else
            {
                ::new(data()) T(t);
                _init = true;
            }
            return *this;
        }

        /** \brief Assigns an instance from an existing instance of `T` with move semantic.
        *
        * When this MemBuf is not initialized, it behaves like the move
        *   constructor of `T` .
        * Otherwise, it delegates the call to the move-assignment operator of `T`.
        */
        this_t& operator = (T&& t) 
            noexcept(std::is_nothrow_constructible_v<T, T&&> && std::is_nothrow_assignable_v<T, T&&>)    
        {
            if( _init )
            {
                *data() = std::move(t);
            }
            else
            {
                ::new(data()) T(std::move(t));
                _init = true;
            }
            return *this;
        }

        /**
        *   \brief construct new object of type `T` from arguments `Ux...`
        *
        * When this MemBuf is not initialized, it behaves like the postponed constructor and creates new instance of `T`.
        * Otherwise, current instance of `T` is destroyed first then constructor is applied.
        */
        template <class ... Ux>
        this_t& construct(Ux&& ... ux) 
            noexcept(std::is_nothrow_constructible_v<T, Ux...> && std::is_nothrow_destructible_v<T>)
        {
            destroy();
            ::new(data()) T(std::forward<Ux>(ux)...);
            _init = true;
            return *this;
        }

        /** \brief Provide low-level access to occupied memory without additional 
        *   checks if object has been initialized. 
        */
        T* data() noexcept
        {
             return _data.data();
        }

        /** \brief Provide low-level constant access to occupied memory without 
        * additional checks if object has been initialized. 
        */
        const T* data() const noexcept
        {
             return _data.data();
        }

        /**
        *   \brief conditionally destroy current contained object
        */
        void destroy() noexcept(std::is_nothrow_destructible_v<T>)
        {
            if( _init )
            {
                std::destroy_at(data());
                _init = false;
            }
        }

    private:

        bool _init = false;
        MemoryAlignedStorage<T> _data;
    };
            


    struct IndexedConstructor
    {
        size_t _type_index;
        constexpr explicit IndexedConstructor(size_t type_index)
            : _type_index(type_index)
        {}
    };

    /**
     * \brief Similar to MemBuf creates object in internal memory buffer, but allows specifying 
     *   multiple possible implementations of a single interface
     *   and exposes access to concrete instances through polymorphism.
     *
     * This class allocates a byte buffer big enough to accommodate any possible implementation 
     *   of `TInterface`, taking into account the current platform's alignment requirements.
     *
     *   \tparam TInterface The exposed polymorphic interface.
     *   \tparam Tx... Multiple implementations of TInterface.
     *  Usage example:
     *  \code
     *      struct Interface
     *      { virtual void method() = 0; }
     *      struct A: Interface
     *      {
     *          virtual void method() override {std::cout << "I'm A\n";}
     *      };
     *      struct B: Interface
     *      {
     *          virtual void method() override {std::cout << "I'm B\n";}
     *      };
     *      ...
     *      using multimpl_t = Multiimplementation<Interface, A, B>;
     *      multimpl_t reserved_memory;
     *      reserved_memory.create<B>(); // now we can access Interface implemented by B
     *      reserved_memory->method();   // prints: "I'm B\n"
     *      reserved_memory.destroy();   // destroy previous instance
     *      reserved_memory.create<A>(); // then access Interface implemented by A
     *      reserved_memory->method();   // prints: "I'm A\n"
     *  \endcode
     */
    template <class TInterface, class ... Tx>
    struct Multiimplementation
    {
        static_assert( std::conjunction_v<std::is_base_of<TInterface, Tx>...>, 
            "All Tx... must inherit base interface TInterface");

        using this_t = Multiimplementation<TInterface, Tx...>;
        
        /** Create uninitialized in memory buffer (no heap operations is involved) */
        constexpr Multiimplementation() 
            noexcept(std::is_nothrow_default_constructible_v<data_store_t>) = default;
        
        /** \brief Copy constructor */
        explicit Multiimplementation(const Multiimplementation& other)
            noexcept(std::is_nothrow_copy_constructible_v<data_store_t>)
            : _data(other._data)
        {
        }

        /** \brief Move constructor */
        explicit Multiimplementation(Multiimplementation&& other) 
            noexcept(std::is_nothrow_move_constructible_v<data_store_t>)
            : _data(std::move(other._data))
        {
        }

        /** \brief Constructor creates instance from implementation `U` on condition U is the same as one of Tx... */
        template <class U>
        explicit Multiimplementation(const U& instance) 
            noexcept(std::is_nothrow_constructible_v<data_store_t, const U&>)
            : _data(instance)
        {
            //static_assert(
            //    ((std::is_same_v<Tx, U>) || ...),
            //    "Cannot cast type U to any of the declared implementations.");
        }

        /** \brief Constructor creates instance from implementation `U` with move semantic 
        *   on condition U is the same as one of Tx... 
        */
        template <class U>
        explicit Multiimplementation(U&& instance)
            noexcept(std::is_nothrow_constructible_v<data_store_t, decltype(instance)>)
            : _data(std::forward<U>(instance))
        {
        }

        ~Multiimplementation()
        {
        }

        /** copy assign from other instance */
        [[maybe_unused]] this_t& operator = (const this_t& other)
            noexcept(std::is_nothrow_copy_assignable_v<data_store_t>)
        {
            _data = other._data;
            return *this;
        }

        /** move assign from other instance */
        [[maybe_unused]] this_t& operator = (this_t&& other) 
            noexcept(std::is_nothrow_move_assignable_v<data_store_t>)
        {
            _data = std::move(other._data);
            return *this;
        }

        /** \brief Assigns an instance from an existing instance of `U`.
        *
        * When `has_value() == false` it behaves like the copy
        *   constructor of `U` .
        * Otherwise, it delegates the call to the move-assignment operator of `U`.
        */
        template <class U>
        [[maybe_unused]] this_t& operator = (const U& t)
            noexcept(std::is_nothrow_assignable_v<data_store_t, const U&>)
        {
            _data = t;
            return *this;
        }

        /** \brief Assigns an instance from an existing instance of `U` with move semantic.
        *
        * When `has_value() == false` it behaves like the move
        *   constructor of `U` .
        * Otherwise, it delegates the call to the move-assignment operator of `U`.
        */
        template <class U, std::enable_if_t<!std::is_same_v<U, this_t>>>
        [[maybe_unused]] this_t& operator = (U&& t)
            noexcept(std::is_nothrow_assignable_v<data_store_t, decltype(t)>)
        {
            _data = std::move(t);
            return *this;
        }

        /** check if buffer contains initialized instance of `T` */
        [[nodiscard]] constexpr bool has_value() const noexcept
        {
            return !std::holds_alternative<std::monostate>(_data);
        }

        /** check if buffer contains initialized instance of `T` */
        [[nodiscard]] constexpr operator bool() const noexcept
        {
            return has_value();
        }
        
        /** check if buffer does not contain initialized instance of `T` */
        [[nodiscard]] constexpr bool operator !() const noexcept
        {
            return !has_value();
        }
        
        /** Construct in-place type `U` on condition `U` is one of the `Tx`...
         * \tparam in-place type `U` must be one of enumerated in this class declarations.
         * \tparam Args argument of constructor `U`
         * \throws std::runtime_error when container already owns an instance (#destroy it first).
         * \return instance of U& that can be ignored or used to after-construction access to 
         *      implementation specific details. For example: \code
         *      
         *      \endcode
         */
        template <class U, class ...Args>
        [[maybe_unused]] U& construct(Args&& ...arg)
        {
            return _data.emplace<U>(std::forward<Args>(arg)...);
        }

        [[nodiscard]] TInterface* operator ->() 
        {
             return get();
        }

        [[nodiscard]] const TInterface* operator ->() const
        {
            return get();
        }

        [[nodiscard]] TInterface* get() 
        {
            if (!has_value())
                throw not_initialized_error{};

            return std::visit([](auto& instance) -> TInterface* {
                if constexpr (std::is_same_v<std::monostate, std::decay_t<decltype(instance)>>)
                {//impossible case, just to calm-down compiler as `!has_value` already excluded this
                    return nullptr;
                }
                else
                {
                    return &instance;
                }
                
                }, _data);
        }

        [[nodiscard]] const TInterface* get() const
        {
            if (!has_value())
                throw not_initialized_error{};

            return std::visit([](const auto& instance) -> const TInterface* {
                if constexpr (std::is_same_v<std::monostate, std::decay_t<decltype(instance)>>)
                {//impossible case, just to calm-down compiler as `!has_value` already excluded this
                    return nullptr;
                }
                else
                {
                    return &instance;
                }

                }, _data);
        }

        [[nodiscard]] TInterface& operator *()
        {
            return *get();
        }
        
        [[nodiscard]] const TInterface& operator *() const
        {
            return *get();
        }

        void destroy()
        {
            _data = std::monostate{};
        }

    private:
        using data_store_t = std::variant<std::monostate, Tx...>;

        data_store_t _data = {};
        
    };

}//ns:contata5::common
#endif //_OP_COMMON_STACKALLOC__H_
