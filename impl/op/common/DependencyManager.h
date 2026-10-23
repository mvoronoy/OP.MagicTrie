#pragma once
#ifndef _OP_COMMON_DEPENDENCYMANAGER__H_
#define _OP_COMMON_DEPENDENCYMANAGER__H_

#include <memory>
#include <optional>
#include <cassert>

#include <op/common/Deref.h>
#include <op/common/Utils.h>
#include <op/common/StackAlloc.h>
#include <op/common/SequenceReorder.h>

namespace OP::common
{

    template <auto ...depends_on>
    class DependencyManager;

    template <typename T>
    concept is_derived_from_dependency_manager = requires(T & object) {
        [] <auto... ax>(DependencyManager<ax...>&) {}(object);
    };

    class ServiceRegistry;

    namespace details
    {
        using memory_retainer_t = std::unique_ptr<void, void(*)(void*)>;

        template <class T>
        class Wrap
        {
            constexpr static inline bool is_std_ref_c = OP::utils::is_generic<T, std::reference_wrapper>::value;
            
            template <class U>
            static inline U& at_impl(OP::MemBuf<U>& value) noexcept
            {
                return *value;
            }
            template <class U>
            static inline U& at_impl(OP::MemBuf<std::reference_wrapper<U>>& value) noexcept
            {
                return value->get();
            }

            unsigned _delete_priority = 0;
            bool _deleted = false;
            OP::MemBuf<T> _value;

        public:

            using element_t = std::remove_reference_t<
                decltype(at_impl(std::declval<OP::MemBuf<T>&>()))>;
            
            template <class U>
            Wrap(U&& value)
                : _value{ std::forward<U>(value) }
            {
            }
            
            Wrap(Wrap&&) = default;
            Wrap(const Wrap&) = delete;

            ~Wrap()
            {
                destroy();
            }

            void destroy() noexcept
            {
                if (!_deleted)
                {
                    _value.destroy();
                    _deleted = true;
                }
            }
            
            element_t& at()
            {
                assert(!_deleted);
                return at_impl(_value);
            }

        };

        template <class ...Tx>
        auto make_wrap_tuple(Tx ...tx)
        {
            return std::tuple{ Wrap<std::decay_t<Tx>>{std::forward<Tx>(tx)}... };
        }
    } //ns:details

    /**
    * @brief Owns a fully constructed dependency graph.
    *
    * Stores every service participating in a dependency resolution session.
    * This class keeps only references to the service objects, while 
    * lifetime control delegated to ServiceRegistry.
    *
    * Instances of this class are created exclusively by ServiceRegistry.
    */
    template <class Tuple>
    class DependencyGraph
    {
        using this_t = DependencyGraph<Tuple>;

        using instances_tuple_t = Tuple;
        /** Metaprogramming predicate to reorder elements inside `Tuple` to respect dependencies.
        * First come independent services, last come most dependent DependencyManager
        */
        struct ReorderServicePredicate 
        {

            template <typename T>
            constexpr static bool is_derived_from_dependency_manager_v = is_derived_from_dependency_manager<T>;

            template <class Ti, class Tj>
            constexpr static std::partial_ordering estimate(
                const details::Wrap<Ti>*left, const details::Wrap<Tj>*right) noexcept
            {
                using left_t = std::decay_t<OP::common::dereference_t<Ti>>;
                using right_t = std::decay_t<OP::common::dereference_t<Tj>>;
                if constexpr (is_derived_from_dependency_manager_v<left_t>)
                {
                    if constexpr (is_derived_from_dependency_manager_v<right_t>)
                    {
                        return
                            left_t::template has_dependency_of_v<right_t>
                            ? std::partial_ordering::greater //move dependent object right
                            : (right_t::template has_dependency_of_v<left_t> 
                                ? std::partial_ordering::less
                                : std::partial_ordering::unordered
                            );
                    }
                    else
                        return
                        left_t::template has_dependency_of_v<right_t>
                        ? std::partial_ordering::greater //move dependent object right
                        : std::partial_ordering::unordered;
                }
                else if constexpr (is_derived_from_dependency_manager_v<right_t>)
                { //pretty sure Ti is not DependencyManager
                    return right_t::template has_dependency_of_v<left_t>
                        ? std::partial_ordering::less
                        : std::partial_ordering::unordered;
                }
                else
                    return std::partial_ordering::unordered;
            }


            template <size_t I, size_t J>
            constexpr static std::partial_ordering predicate = estimate(
                static_cast<std::add_pointer_t<std::tuple_element_t<I, Tuple>>>(nullptr),
                static_cast<std::add_pointer_t<std::tuple_element_t<J, Tuple>>>(nullptr)
            );
                
        };
        using direct_index_t = decltype(SequenceReorder{ std::make_index_sequence<std::tuple_size_v<Tuple>>{} });

        friend ServiceRegistry;

        Tuple _tuple;
        details::memory_retainer_t _previous;

        DependencyGraph(details::memory_retainer_t&& previous, Tuple&& tuple) noexcept
            : _tuple(std::move(tuple))
            , _previous(std::move(previous))
        {
        }

        template <auto ...ux>
        void resolve_dependencies(DependencyManager<ux...>* service) noexcept
        {
            service->kick(_tuple);
        }

        constexpr void resolve_dependencies(...) noexcept
        {/*do nothing for generic parameter*/
        }

        template <auto ...ux>
        void call_virtual_constructor(DependencyManager<ux...>* service)
        {
            service->on_complete();
        }

        constexpr void call_virtual_constructor(...) noexcept
        {/*do nothing for generic parameter*/
        }

        constexpr void kick() noexcept
        {
            //resolve all dependencies
            constructor_order_index_t::apply(
                [this](auto& ...instance) {
                    (resolve_dependencies(&OP::common::get_reference(instance.at())), ...);
                },
                _tuple
            );
        }

        constexpr void when_complete()
        {
            //call virtual constructors
            constructor_order_index_t::apply(
                [this](auto&... instance) {
                (call_virtual_constructor(&OP::common::get_reference(instance.at())), ...);
                },
                _tuple
            );
        }
        /** Apply destroying sequence in opposite order than constructors' sequence */
        void shutdown() noexcept
        {
            destructor_order_index_t::apply(
                [](auto&... instance) { //call Wrap::destroy
                    (instance.destroy(), ...);
                },
                _tuple
            );
        }

        //get core type pointer behind all wrappers and references
        template<class U>
        using unify_t = std::add_pointer_t<std::decay_t<OP::common::dereference_t<U>>>;

        template <class U>
        struct NarrowPointerCastTypeMap
        {
            //what is target type
            using core_t = unify_t<U>;

            //narrow and make type most generic
            template <typename T>
            using map_t = std::conditional_t<
                std::is_convertible_v<unify_t<typename T::element_t>, core_t>,
                core_t, //can use generic type
                unify_t<T> //use as is
            >;
        };

    public:
        using constructor_order_index_t = decltype(direct_index_t::template reorder<ReorderServicePredicate>());
        using destructor_order_index_t = decltype(constructor_order_index_t::reverse());

        ~DependencyGraph()
        {
            shutdown();
        }

        /**
        * @brief Returns a service stored in the dependency graph.
        *
        * @tparam T Exact stored service type.
        * @return Reference to the requested service.
        */
        template <class T>
        [[nodiscard]] constexpr auto& get() noexcept
        {
            using element_t = std::decay_t<T>;
            using wrap_t = details::Wrap<element_t>;
            if constexpr( OP::utils::contains_type_v<wrap_t, Tuple> )
                return OP::common::get_reference(std::get<wrap_t>(_tuple).at());
            else //try explore tuple using `get_reference` type resolving technique 
            {
                using namespace OP::utils;
                using dep_t = unify_t<T>;
                using reference_only_tuple_t = typename MapType<NarrowPointerCastTypeMap<T>, Tuple>::type;
                static_assert(
                    OP::utils::contains_type_v<dep_t, reference_only_tuple_t>,
                    "DependencyGraph::get<T> cannot resolve queried type from Dependency Graph even after dereferencing");
                constexpr size_t type_index = tuple_ref_index<dep_t, reference_only_tuple_t>::value;
                return OP::common::get_reference(std::get<type_index>(_tuple).at());
            }
        }
    };

    /**
    * @brief Owns service lifetime and performs dependency resolution.
    *
    * ServiceRegistry assembles a graph of service instances, resolves declared
    * dependencies between them and invokes the virtual construction phase of every
    * DependencyManager-derived object.
    *
    * Services supplied as owning smart pointers or temporary objects become owned
    * by the registry for the lifetime of the returned DependencyGraph. Services
    * supplied by reference remain externally owned.
    *
    * Each call to resolve_dependencies() creates an independent service graph
    * while preserving ownership of the previously created graph until it becomes
    * unreachable.
    */
    class ServiceRegistry 
    {

        template <class T>
        static void custom_deleter(void* instance) noexcept
        {
            delete reinterpret_cast<T*>(instance);
        }
        
        template <class T>
        [[maybe_unused]] T& setup_retainer(T* instance)
        {
            auto service_set = details::memory_retainer_t(instance, &custom_deleter<T>);
            instance->kick();
            _memory_retainer = std::move(service_set);
            //instance->when_complete();
            return *instance;
        }

        details::memory_retainer_t _memory_retainer{nullptr, nullptr};

    public:

        /**
        * @brief Builds a service graph and resolves all declared dependencies.
        *
        * Accepts arbitrary service implementations, references or smart pointers,
        * constructs an internal DependencyGraph, injects all matching dependencies into
        * DependencyManager-derived objects and finally invokes their virtual
        * constructors.
        *
        * Missing services may be synthesized automatically when a corresponding
        * `or_default_factory()` declaration exists.
        *
        * Example:
        * @code
        * ServiceRegistry registry;
        *
        * auto& graph = registry.resolve_dependencies(
        *     std::unique_ptr<Logger>{new LoggerImpl},
        *     std::ref(config),
        *     ThreadPool{}
        * );
        *
        * graph.get<Application>().run();
        * @endcode
        *
        * @param services Initial set of service instances.
        * @return Reference to the created DependencyGraph containing every service.
        */
        template <class ...Tx>
        auto& resolve_dependencies(Tx&& ...services)
        {
            auto& result = setup_retainer(
                new DependencyGraph(
                    std::move(_memory_retainer),
                    details::make_wrap_tuple(std::forward<Tx>(services)...))
            );
            result.when_complete();
            return result;
        }
    };

    struct MetaMarker { enum Order { priority = 1, alternatives = 2}; };

    /**
    * @brief Declares a mandatory external dependency.
    *
    * Instances are intended only as compile-time markers passed to
    * DependencyManager. They indicate that an implementation of the specified
    * service type must be supplied during dependency resolution unless another
    * declaration provides a compatible fallback factory.
    *
    * @tparam T Required service interface.
    */
    template <class T>
    struct Expecting : MetaMarker
    {
        using expected_t = T;
        using base_t = Expecting<expected_t>;
        constexpr static inline unsigned priority_order_c = MetaMarker::priority;
    };

    /**
    * @brief Creates a compile-time dependency declaration.
    *
    * Convenience helper used in DependencyManager template argument lists.
    *
    * Example:
    * @code
    * DependencyManager<
    *     expecting<Logger>(),
    *     expecting<Scheduler>()
    * >
    * @endcode
    */
    template <class T>
    inline constexpr auto expecting() noexcept
    {
        return Expecting<T>{};
    }

    /**
    * @brief Declares a fallback factory for a dependency.
    *
    * Associates a service type with a factory that is invoked only when no
    * externally supplied implementation satisfies the dependency.
    *
    * The factory is evaluated lazily and at most once for each dependency graph.
    * The created object is cached and reused for all matching dependencies.
    *
    * @tparam F Factory functor returning a service instance.
    */
    template <class F>
    struct OrDefaultFactory : Expecting<std::invoke_result_t<F>>
    {
        using base_t = typename Expecting<std::invoke_result_t<F>>::base_t;
        using expected_t = typename base_t::expected_t;
        constexpr static inline unsigned priority_order_c = MetaMarker::alternatives;

        F _factory;
        template <class U>
        constexpr explicit OrDefaultFactory(U&& factory) noexcept
            : _factory(std::forward<U>(factory))
        {
        }

        constexpr expected_t operator()() const noexcept(std::is_nothrow_invocable_v<F>)
        {
            return _factory();
        }
    };

    /**
    * @brief Creates a lazy fallback dependency factory.
    *
    * Registers a factory that constructs a default implementation whenever the
    * corresponding service is not provided explicitly during dependency
    * resolution.
    *
    * @param factory Zero-argument callable returning a service object.
    * @return Compile-time dependency declaration suitable for DependencyManager.
    */
    template <class F>
    inline constexpr auto or_default_factory(F&& factory) noexcept
    {
        static_assert(
            std::invocable<F> &&
            (!std::same_as<std::invoke_result_t<F>, void>), "Factory must be a functor with zero args");
        return OrDefaultFactory<F>{std::forward<F>(factory)};
    }

    /**
    * @brief Declares and resolves compile-time service dependencies.
    *
    * `DependencyManager` implements a lightweight dependency injection mechanism
    * based entirely on compile-time dependency declarations. Classes deriving from
    * this template specify the services they require using `expecting<T>()`
    * declarations and may optionally provide fallback construction rules using
    * `or_default_factory()`.
    *
    * Dependency resolution is performed by ServiceRegistry after all participating
    * objects have been created. Once every dependency has been assigned,
    * DependencyManager invokes the protected virtual callback `on_complete()`,
    * effectively implementing a "virtual constructor" design pattern.
    *
    * This approach is particularly useful for classes that depend on numerous
    * cross-cutting services such as logging, memory managers, schedulers,
    * executors, telemetry providers, or configuration services, while avoiding
    * constructor parameter explosion.
    *
    * Example:
    * @code
    * struct Database :
    *     DependencyManager<
    *         expecting<Logger>(),
    *         expecting<ThreadPool>(),
    *         or_default_factory(make_default_metrics)>
    * {
    *     using DependencyManager::base_t;
    *
    * protected:
    *     void on_complete() override
    *     {
    *         resolve<Logger>().write("initialized");
    *     }
    * };
    * @endcode
    *
    * @tparam depends_on Dependency declarations produced by
    *         expecting<T>() and/or or_default_factory().
    */
    template <auto ... depends_on>
    class DependencyManager 
    {
        static_assert(
            (std::is_base_of_v<MetaMarker, std::decay_t<decltype(depends_on)>> && ...),
            "Dependency declaration should use special functions like `expecting<T>()`, `or_default_factory(...)`, `or_default(...)`..."
            );

        friend ServiceRegistry;
        
        template <class Tuple>
        friend class DependencyGraph;

        /** Metaprogramming predicate to elect priority type definitions */
        template <typename MetaMarker::Order filter_order>
        struct PrioritySearchTypeFilter
        {
            template <class T>
                requires std::derived_from<T, MetaMarker>
            static constexpr bool check = (T::priority_order_c == filter_order);
        };

        template <class T>
        using pointer_once_t = std::conditional_t<
            std::is_pointer_v<T>, T, std::add_pointer_t<T>
        >;
        template <class Expected>
        struct TypeFactoryTypeFilter
        {
            template <class T>
            static constexpr bool check = (T::priority_order_c == MetaMarker::alternatives) && 
                std::is_convertible_v<pointer_once_t<typename T::expected_t>, pointer_once_t<Expected>>;
        };

        /** Metaprogramming predicate to help 2 tuples contain unique types only*/
        template <class FirstTuple>
        struct UniqueTypeFilter
        {
            template <class T>
            static constexpr bool check = !OP::utils::contains_type_v<T, FirstTuple>;
        };
        
        using all_dependecies_t = std::tuple<decltype(depends_on)...>;
        using first_class_dependecies_t =
            OP::utils::type_filter_t<PrioritySearchTypeFilter<MetaMarker::priority>, all_dependecies_t>;
        using alt_class_dependencies_t = 
            OP::utils::type_filter_t<PrioritySearchTypeFilter<MetaMarker::alternatives>, all_dependecies_t>;
        
        template <class Dep>
        using _MapDep2Service = pointer_once_t<typename Dep::expected_t>;

        using first_class_tuple_t = typename OP::utils::MapType<
            OP::utils::SimpleTypeMap<_MapDep2Service>,
            first_class_dependecies_t>::type;

        using alt_class_tuple_t = typename OP::utils::MapType<
            OP::utils::SimpleTypeMap<_MapDep2Service>,
            alt_class_dependencies_t>::type;
        /** Inside single dependency Graph all services are stored as pointers to the global instances*/
        using local_services_t = OP::utils::tuple_merge_t<
            first_class_tuple_t, 
            OP::utils::type_filter_t<
                UniqueTypeFilter<first_class_tuple_t>,
                alt_class_tuple_t
            >>;


        local_services_t _local_services{};

        template <class Dep>
        using _MapAlt2Optional = std::optional<typename Dep::expected_t>;
        //need placeholder (tuple) for values obtained from factories, so reserve std::optional to keep them
        using optional_placeholders_t = typename OP::utils::MapType<
            OP::utils::SimpleTypeMap<_MapAlt2Optional>,
            alt_class_dependencies_t>::type;

        optional_placeholders_t _optional_factory_results;

        /** Metaprogramming predicate to select candidates for service matched to `Expected` type */
        template <class Expected>
        struct ServiceSearchTypeFilter
        {
            template <class T>
            static constexpr bool check = std::is_convertible_v<
                // T there is details::Wrap, so access real type by `element_t`
                pointer_once_t<OP::common::dereference_t<typename T::element_t>>,
                pointer_once_t<OP::common::dereference_t<Expected>> >;
        };

    public:
        constexpr static inline auto depends_on_c = std::make_tuple(depends_on...);

        template <class T>
        constexpr bool static eval_dep() noexcept
        {
            using decayed_t = std::decay_t<dereference_t<T>>;
            auto act = [](const auto& d) {
                using expecting_t = std::decay_t<decltype(d)>::expected_t;
                return (std::is_base_of_v<expecting_t, decayed_t> || std::is_same_v<expecting_t, decayed_t>);
                };
            return std::apply(
                [&](const auto& ... dx) {
                    return (act(dx) || ...);
                },
                depends_on_c
            );
        }
    
        /** Test if this class depends on type T */
        template <class T>
        constexpr static inline bool has_dependency_of_v = eval_dep<T>()
            ;



        template <class U>
        void alternative_assign(U*& cached_service) noexcept
        {
            // [DEBUG]....
            //using zeroel_t = std::tuple_element_t<0, alt_class_dependencies_t>;
            //std::cout << "size(alt_class_dependencies_t)=" << std::tuple_size_v<alt_class_dependencies_t> << '\n'
            //    << "order="<<zeroel_t::priority_order_c  << '\n'
            //    << "type(U)=" << typeid(std::add_pointer_t<U>).name() <<'\n'
            //    << "type(F::expected)=" << typeid(std::add_pointer_t<typename zeroel_t::expected_t>).name() << '\n'
            //    << "is_convertible=" << std::is_convertible_v<std::add_pointer_t<typename zeroel_t::expected_t>, std::add_pointer_t<U>>
            //    << "\n";

            using factories_t = OP::utils::type_filter_t<TypeFactoryTypeFilter<U>, alt_class_dependencies_t>;
            static_assert(std::tuple_size_v<factories_t> > 0,
                "There is no rule to resolve dependency for type U");
            using local_factory_t = std::tuple_element_t<0, factories_t>;
            using optional_holder_t = _MapAlt2Optional<local_factory_t>;
            auto& holder = std::get<optional_holder_t>(_optional_factory_results);
            if (!holder.has_value())
                holder.emplace(std::get<local_factory_t>(depends_on_c)());
            cached_service = &holder.value();
        }

        /** 0..1 tuple of candidates from `local_services_t` that matched to `U` */
        template <class U>
        using candidates_t = OP::utils::type_filter_t<ServiceSearchTypeFilter<U>, local_services_t>;


        template <class U, class Tuple>
        void assign_service(U*& cached_service, Tuple& known_services) noexcept
        {
            using locals_t = OP::utils::type_filter_t<ServiceSearchTypeFilter<U>, Tuple>;
            if constexpr(std::tuple_size_v<locals_t> > 0)
            { // instance already exists in service-list
                using element_t = std::tuple_element_t<0, locals_t>;
                cached_service = &OP::common::get_reference(std::get<element_t>(known_services).at());
            }
            else //try resolve by applying alternatives
            {
                alternative_assign(cached_service);
            }
        }

        template <class Tuple>
        constexpr void kick(Tuple& other_services) noexcept
        {
            unsigned assigned = 0;
            std::apply([&](auto*& ...cached_service) {
                (assign_service(cached_service, other_services), ...);
                },
                _local_services
            );
        }


    public:

        template <class U>
        using service_match_t = std::tuple_element_t<0, candidates_t<U>>;

        using base_t = DependencyManager<depends_on...>;

    public:

        /**
        * @brief Returns a resolved dependency.
        *
        * Retrieves the implementation associated with the requested service type.
        * All dependencies are guaranteed to be available after dependency resolution
        * has completed.
        *
        * Calling this function before ServiceRegistry has finished resolving
        * dependencies results in undefined behavior.
        *
        * @tparam U Requested service interface.
        * @return Reference to the resolved service implementation.
        */
        template <class U>
        constexpr U& resolve() noexcept
        {
            return *std::get< std::add_pointer_t<U>>(_local_services);
        }

        template <class U>
        constexpr const U& resolve() const noexcept
        {
            return *std::get< std::add_pointer_t<U>>(_local_services);
        }

    protected:

        /**
        * @brief Virtual constructor executed after dependency injection completes.
        *
        * ServiceRegistry invokes this callback exactly once after every declared
        * dependency has been resolved and assigned.
        *
        * Derived classes should override this function instead of performing
        * dependency-dependent initialization in their ordinary constructor, since
        * constructor execution occurs before dependency injection.
        *
        * The default implementation performs no action.
        */
        virtual void on_complete() 
        {
            /*default implementation: does nothing*/
        }
    };

}//ns:op::common

#endif //_OP_COMMON_DEPENDENCYMANAGER__H_
