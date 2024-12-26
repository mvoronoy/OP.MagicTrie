#pragma once
#ifndef _OP_COMMON_STACKALLOC__H_
#define _OP_COMMON_STACKALLOC__H_

#include <cstdint>
#include <stdexcept>

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
            ::new(_data) T(std::forward<Ux>(ux)...);
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
                ::new(_data) T(*other.data());
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
                ::new(_data) T(std::move(*other.data()));
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
                ::new(_data) T(t);
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
                ::new(_data) T(std::move(t));
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
            ::new(_data) T(std::forward<Ux>(ux)...);
            _init = true;
            return *this;
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
        alignas(T) std::byte _data[sizeof(T)] = {};
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

        /** const used to indicate invalid contained index */
        constexpr static size_t npos_c = sizeof...(Tx);

        using this_t = Multiimplementation<TInterface, Tx...>;
        
        /** Create uninitialized in memory buffer (no heap operations is involved) */
        constexpr Multiimplementation() noexcept = default;
        
        /** \brief Copy constructor */
        explicit Multiimplementation(const Multiimplementation& other)
        {
            if(other.has_value())
            {
                //no worries const_cast isn't for object change
                const_cast<this_t&>(other)
                    .apply_by_index_impl(
                        other._type_index, 
                        [this](auto* existing)
                        {
                            using t_t = std::decay_t<decltype(*existing)>;
                            ::new(_data) t_t(*const_cast<const t_t*>(existing)); //call origin copy constructor
                        },
                        std::make_index_sequence<sizeof...(Tx)>()
                        );
                _type_index = other._type_index;
            }
        }

        /** \brief Move constructor */
        explicit Multiimplementation(Multiimplementation&& other) noexcept
        {
            if(other.has_value())
            {
                //no worries const_cast isn't for object change
                other
                    .apply_by_index_impl(
                        other._type_index, 
                        [this](auto* existing)
                        {
                            using t_t = std::decay_t<decltype(*existing)>;
                            ::new(_data) t_t(std::move(*existing)); //call origin move constructor
                        },
                        std::make_index_sequence<sizeof...(Tx)>());
                _type_index = other._type_index;
                other._type_index = npos_c;
            }
        }

        /** \brief Constructor creates instance from implementation `U` on condition U is the same as one of Tx... */
        template <class U>
        explicit Multiimplementation(const U& instance) 
        {
            static_assert(
                ((std::is_same_v<Tx, U>) || ...),
                "Cannot cast type U to any of the declared implementations.");
            ::new(_data) U(instance);
            _type_index = type_to_index<U>(std::index_sequence_for <Tx...>{});
        }

        /** \brief Constructor creates instance from implementation `U` with move semantic 
        *   on condition U is the same as one of Tx... 
        */
        template <class U>
        explicit Multiimplementation(U&& instance) noexcept
        {
            using plain_u = std::decay_t<U>;
            static_assert(
                std::disjunction_v<std::is_same<plain_u, Tx>...>,
                "Cannot cast type U to any of the declared implementations.");
            ::new(_data) plain_u(std::move(instance));
            _type_index = type_to_index<plain_u>(std::index_sequence_for <Tx...>{});
        }

        ~Multiimplementation()
        {
            destroy();
        }

        /** copy assign from other instance */
        [[maybe_unused]] this_t& operator = (const this_t& t)
        {
            if (t.has_value())
            {
                //no worries `t` used as const 
                const_cast<this_t&>(t).apply_by_index_impl(
                    t._type_index,
                    [this](auto* v) {
                        using v_t = std::decay_t<decltype(*v)>;
                        this->assign(*const_cast<v_t*>(v));
                    },
                    std::index_sequence_for <Tx...>{});
            }
            else //revert to unassigned state
            {
                destroy();
            }
            return *this;
        }

        /** move assign from other instance */
        [[maybe_unused]] this_t& operator = (this_t&& t) noexcept
        {
            if (t.has_value())
            {
                //no worries `t` used as const 
                t.apply_by_index_impl(
                    t._type_index,
                    [this](auto* v) {
                        using v_t = std::decay_t<decltype(*v)>;
                        this->assign(std::move(*v));
                    },
                    std::index_sequence_for <Tx...>{});
                t._type_index = npos_c; //reset source
            }
            else //revert to unassigned state
            {
                destroy();
            }
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
        {
            assign(t);
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
        {
            assign(std::move(t));
            return *this;
        }

        /** check if buffer contains initialized instance of `T` */
        [[nodiscard]] constexpr bool has_value() const noexcept
        {
            return _type_index < npos_c;
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
            static_assert(
                 std::disjunction_v<std::is_same<U, Tx>...>, 
                 "Try to construct type U not defined in Tx... list");
            if( has_value() )
                throw std::runtime_error("Instance is already initialized, destroy it first");
            U *result = ::new(_data) U(std::forward<Args>(arg)...);
            _type_index = type_to_index<U>(std::make_index_sequence<sizeof...(Tx)>{});
            return *result;
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
            if(!has_value())
                throw not_initialized_error{};
            return data();
        }

        [[nodiscard]] const TInterface* get() const noexcept
        {
            return const_cast<this_t*>(this)->get();
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
            if( has_value() )
            {
                if constexpr( std::has_virtual_destructor_v<TInterface> )
                    std::destroy_at(data());
                else //need call correct destructor as soon interface don't expose virtual
                {
                    apply_by_index_impl(
                        _type_index, 
                       [&](auto* t){ 
                            std::destroy_at(t);
                        },
                        std::make_index_sequence<sizeof...(Tx)>());
                }
                _type_index = npos_c;
            }
        }

    private:

        using pack_t = std::tuple<Tx...>;

        template <size_t I>
        using target_t = std::tuple_element_t<I, pack_t>;

        size_t _type_index = npos_c;
        
        TInterface* data() noexcept
        {
            return std::launder(reinterpret_cast<TInterface*>(_data));
        }

        template <class U, size_t ...Ix>
        static constexpr size_t type_to_index(std::index_sequence<Ix...>)
        {
            using pack_t = std::tuple<Tx...>;
            return ((std::is_same_v<U, std::tuple_element_t<Ix, pack_t>> ? Ix : 0) | ...);
        }

        template <class F, size_t ...Ix>
        constexpr void apply_by_index_impl(size_t index, F applicator, std::index_sequence<Ix...>)
        {
            //use && to laverage immediate stop when functor was applied
            bool is_apply_succeed = ((index != Ix ||
                (applicator( std::launder(reinterpret_cast<target_t<Ix>*>(_data))), false ) ) 
            && ...);
            static_cast<void>(is_apply_succeed);//hide unused warning
        }

        template <class U>
        void assign(U&& u)
        {
            using plain_u = std::decay_t<U>;
            static_assert(
                std::disjunction_v<std::is_same<plain_u, Tx>...>,
                "Cannot cast type U to any of the declared implementations.");
            if (has_value())
            {
                if (_type_index == type_to_index<plain_u>(std::index_sequence_for <Tx...>{}))
                { // support the same type assignment
                    *data() = std::forward<U>(u);
                    return;
                }
                else //need destroy previous
                {
                    destroy();
                }
            }
            // need re-construct value
            construct<plain_u>(u);
        }
        
        static constexpr size_t bigest_align_c = std::max( {alignof(TInterface), alignof(Tx)...} );
        static constexpr size_t bigest_sizeof_c = std::max( {sizeof(TInterface), sizeof(Tx)...} );

        alignas(bigest_align_c) std::byte _data[bigest_sizeof_c]={};
        
    };

}//ns:contata5::common
#endif //_OP_COMMON_STACKALLOC__H_
