#pragma once

#ifndef _OP_COMMON_EVENTSUPPLIER_H_
#define _OP_COMMON_EVENTSUPPLIER_H_

#include <functional>
#include <memory>
#include <mutex>
#include <list>
#include <shared_mutex>
#include <optional>

#include <op/common/Utils.h>
#include <op/common/Currying.h>
#include <op/common/Assoc.h>
#include <op/common/ValueGuard.h>

namespace OP::events
{
    
    /**
     * \brief Polymorphic base for event subscriptions.
     *
     * Subscription<TPayload> represents a callable entity capable of
     * receiving an event payload of type TPayload.
     *
     * \tparam TPayload Event payload type.
     */
    template <class TPayload>
    struct Subscription
    {
        virtual ~Subscription() = default;
        /** \brief Handle the event.
        * \return* - `true`  - continue propagation to next subscriber.
        *          - `false` - stop propagation immediately.
        */
        virtual bool call(const TPayload&) = 0;
    };

    /**
     * \brief Concrete subscription wrapper for arbitrary callable handlers.
     *
     * FunctorSubscription adapts a callable object (lambda, function,
     * member function wrapper, std::bind, etc.) to the Subscription interface.
     *
     * ### Payload Decomposition
     *
     * If the payload is a std::tuple or std::pair, the handler may:
     *
     * - Accept the entire tuple/pair as a single parameter. Example: \code
     *  enum class SomeEv {a};
     *  using a_event = std::tuple<int, float>;
     * 
     *  void handler_of_a(const a_event&) ... 
     * \endcode
     * - Accept decomposed arguments corresponding to individual elements. Example: \code
     *  void handler2_of_a(int num1, float num2) ... 
     * \endcode
     * - Omit trailing arguments. Example: \code
     *  void handler3_of_a(float num2) ... 
     * \endcode
     * - Ignore all arguments entirely. Example: \code
     *  void handler4_of_a() ... 
     * \endcode
     *
     * \tparam F Callable type. If the handler returns bool, then - `true`  - continue propagation to next subscriber.
     *      - `false` - stop propagation immediately.
     *      - Otherwise, the handler is assumed to return void,  and event propagated to all subscribers.
     *
     * \tparam TPayload Event payload type.
     */
    template <class F, class TPayload/* = std::tuple_element_t<0, typename OP::utils::function_traits<F>::arguments_t>*/ >
    struct FunctorSubscription : Subscription<TPayload>
    {
        F _method;
        constexpr FunctorSubscription(F method) noexcept
            : _method(std::move(method))
        {
        }

        virtual bool call(const TPayload& body) override
        {
            if constexpr (std::is_convertible_v<decltype(_call(_method, body)), bool>)
                return _call(_method, body);
            else
            {
                _call(_method, body);
                return true;
            }
        }

    private:
        template <class M2>
        static constexpr decltype(auto) _call(M2& method, const TPayload& body)
        {
            return std::apply(
                [&](const auto& ...argx) {
                    return currying::recomb_call(method, body, argx...);
                },
                body
            );
        }
    };

    /**
     * \brief RAII handle controlling lifetime of a subscription.
     *
     * When the returned std::unique_ptr<Unsubscriber> is destroyed,
     * the corresponding subscription is automatically removed.
     *
     * Users may explicitly call unsubscribe() or simply let the
     * object go out of scope.
     *
     * Thread-safe with respect to concurrent event dispatch and
     * subscription operations.
     */
    struct Unsubscriber
    {
        Unsubscriber() = default;

        Unsubscriber(const Unsubscriber&) = delete;
        Unsubscriber(Unsubscriber&&) = delete;
        Unsubscriber& operator =(const Unsubscriber&) = delete;
        Unsubscriber& operator =(Unsubscriber&&) = delete;

        virtual ~Unsubscriber() = default;
        virtual void unsubscribe() noexcept = 0;
    };

    /**
     * \brief Compile-time typed, thread-safe event broadcasting system.
     *
     * EventSupplier defines a fixed set of event types at compile time.
     * Each event is identified by a unique code (enum, enum-class,
     * integral constant, static string, or any constexpr comparable value)
     * and associated with a specific payload type.
     *
     * The class provides:
     *
     * - Type-safe subscription;
     * - Deterministic invocation order (FIFO by subscription time);
     * - Propagation control: subscribers may stop further event delivery;
     * - Thread-safe event broadcasting via send<EventCode>();
     * - RAII-based unsubscription;
     * - Optional optimized subscription path (`on`) and lookup-based path (`subscribe`).
     *
     * ### Threading Model ####
     *
     * Subscription:
     *   - Multiple threads may concurrently subscribe/unsubscribe.
     *   - Internally protected by std::shared_mutex (read for send, write for subscribe/unsubscribe).
     *
     * Sending:
     *   - Multiple threads may call send() concurrently.
     *   - Ordering of handler execution is unspecified.
     *   - Handlers are invoked under shared lock.
     *
     * Unsubscription:
     *   - Safe while other threads are sending events.
     *   - Occurs automatically when Unsubscriber is destroyed.
     *   - Unsubscribe allowed from within an event handler (directly or indirectly).
     *
     * ### Guarantees ###
     *
     * - Compile-time error if subscribing to an undefined event.
     * - Payload type is strongly bound to event code.
     * - No dynamic type casting required.
     * - Subscription lifetime managed via RAII.
     * - The same handler may subscribe to the same event multiple times. Each subscription is treated
     *      as an independent registration and will receive the event independently.
     *     Example: \code
     *
     *      auto h1 = supplier.on<SomeEv::a>(handler);
     *      auto h2 = supplier.on<SomeEv::a>(handler);
     *      \endcode
     *     When event `SomeEv::a` is sent, `handler` will be invoked twice,
     *     in the order the subscriptions were registered. Unsubscribing one handle does not affect the other.
     *
     *
     * \note Using sending event from multiple threads should be supported by thread-safety of handlers.
     *
     * \tparam TEventDef... Compile-time list of event definitions. Single event is defined using `Assoc<EventCode, PayloadType>` 
     *      and combined together in class definition.For example: \code
     *        enum class SomeEv{a, b, c};
     *        using supplier_t = EventSupplier<
     *            Assoc<SomeEv::a, std::tuple<int>>,
     *            Assoc<SomeEv::b, std::tuple<int, float, const char*>>,
     *            Assoc<SomeEv::c, std::tuple<int, int, int>>
     *        >;
     *        \endcode 
     *       Payload may be:
     *
     *        - std::tuple<...> (and empty tuple as well),
     *        - std::pair<...>,
     *        - user-defined structs,
     *        - plain scalar types.
     *
     */
    template <class ... TEventDef>
    class EventSupplier
    {
        using all_events_t = std::tuple<TEventDef...>;

        template <class TPayload>
        using subscribers_list_t = std::list<Subscription<TPayload>*>;

        template <class TEvent>
        struct SingleEventSubscription
        {
            using event_t = TEvent;
            using event_payload_t = typename TEvent::type;
            using list_t = subscribers_list_t<event_payload_t>;

            std::atomic<size_t> _owned = 0;
            list_t _subscribers;
            std::shared_timed_mutex _subscribers_acc;

            /** special temp storage that collects Subscription marked for delete during event
            * handling. To avoid deadlock on _subscribers_acc unsuccessfully removed subscriptions temporary
            * placed there.
            */ 
            std::vector<std::unique_ptr<Unsubscriber>> _unsubscribe_later;
            std::mutex _unsubscribe_later_acc;
            std::atomic<bool> _has_unsubscribe_later = false;
        };

        template <class TEvent>
        using single_event_subscription_ptr = std::shared_ptr<SingleEventSubscription<TEvent>>;

        /** helper to lookup event type definition by code */
        template <auto EventCode>
        struct PickEvent
        {
            template <class T>
            static constexpr bool check = (T::code == EventCode);
        };

        /**
        * Resolve subscription type by event code.
        *
        * If any compile time error concerned with this line - it means event code `c` is not a registered event.
        */
        template <auto c>
        using sub_by_event_code_t = std::tuple_element_t<
            0, utils::type_filter_t<PickEvent<c>, all_events_t> >;

        template <class TEvent, class TSubscriber>
        struct UnsubImpl : Unsubscriber
        {
            using event_def_t = TEvent;
            using list_t = subscribers_list_t<typename event_def_t::type>;
            using iterator_t = typename list_t::iterator;
            using single_event_sub_ptr = single_event_subscription_ptr<event_def_t>;
                
            template <class ...Ax>
            UnsubImpl(single_event_sub_ptr global_sub, Ax&& ...subscriber_args)
                : _global_sub(std::move(global_sub))
                , _subscriber(std::in_place, std::forward<Ax>(subscriber_args)...)
            {
                std::unique_lock w_guard(_global_sub->_subscribers_acc);
                _subject_pos = _global_sub->_subscribers.emplace(
                        _global_sub->_subscribers.end(), &*_subscriber);
            }

            virtual ~UnsubImpl() noexcept
            {
                _unsub();
            }

            void unsubscribe() noexcept override
            {
                _unsub();
            }

        private:
            UnsubImpl(UnsubImpl&& other) noexcept 
                : _global_sub(std::move(other._global_sub))
                , _subscriber(std::move(other._subscriber))
                , _subject_pos(std::move(other._subject_pos))
            {
                other._subscriber.reset(); // because doc doesn't specify explicitly that value reset during move.
            }

            void _unsub() noexcept
            {
                if (_subscriber.has_value())
                {
                    do
                    {
                        if (_global_sub->_owned)
                        { // case when unsubscribe happens inside same event processing, 
                          //postpone unsubscribe until the end
                            std::unique_lock g(_global_sub->_unsubscribe_later_acc);
                            _global_sub->_has_unsubscribe_later = true;
                            _global_sub->_unsubscribe_later.emplace_back(new UnsubImpl(std::move(*this)));
                            return;
                        }
                        std::unique_lock w_guard(_global_sub->_subscribers_acc, std::defer_lock);
                        if (w_guard.try_lock_for(std::chrono::milliseconds(500)))
                        {
                            _global_sub->_subscribers.erase(_subject_pos);
                            _subscriber.reset();
                            return;
                        }
                    } while (true);
                }
            }

            single_event_sub_ptr _global_sub;
            std::optional<TSubscriber> _subscriber;
            iterator_t _subject_pos;
        };

        // all event-subscriptions are stored in this tuple as shared ptr to allow unsubscribers 
        // work safely. 
        using all_evets_subscription_t = std::tuple< 
            AssocVal<TEventDef::code, single_event_subscription_ptr<TEventDef> > ...>;

        all_evets_subscription_t _subscriptions;

    public:
        
        using unsubscriber_t = std::unique_ptr<Unsubscriber>;

        /** Resolve payload type for specific event code */
        template <auto code>
        using event_payoad_t = typename sub_by_event_code_t<code>::type;

        EventSupplier()
            : _subscriptions{
                new SingleEventSubscription<TEventDef>...
            }
        {
        }

        /** \brief register handler of event
        * 
        * \tparam EventCode Compile-time event identifier. 
        * \tparam F Callable handler type. Use lambdas, functions, member function wrapped with `std::bind`, etc. Optional 
        *       argument can specify a payload of event. If payload is a tuple or pair, handlers may:
        *
        *        - Accept the entire payload as const reference
        *        - Accept decomposed arguments
        *        - Omit some tuple parameters
        *        - Accept no arguments at all
        *
        *        Examples: \code
        *        void handler1(std::tuple<int, float> const&);
        *        void handler2(int, float); // decomposed tuple 
        *        void handler3(int); // omit float parameter of tuple 
        *        void handler4(); // no arg
        *        \endcode
        *
        * \return Unique pointer to Unsubscriber. The returned object must be kept alive to 
        *       continue receiving events. Destroying it automatically unsubscribes.
        */ 
        template <auto EventCode, class F>
        [[nodiscard]] unsubscriber_t on(F f)
        {
            using assoc_to_ptr_t =
                std::tuple_element_t<0, utils::type_filter_t<PickEvent<EventCode>, all_evets_subscription_t> >;
            using event_sub_t = sub_by_event_code_t<EventCode>;
            using payload_t = typename event_sub_t::type;
            using subscriber_t = FunctorSubscription<F, payload_t>;

            auto& event_sub_assoc = std::get<assoc_to_ptr_t >(_subscriptions);
            return std::unique_ptr<Unsubscriber>{
                new UnsubImpl<event_sub_t, subscriber_t>(event_sub_assoc.value, std::move(f)) };
        }

        /** \brief register handler of event.
        * In compare with `on` it uses lookup-based search of code as an argument.
        * \return A unique pointer to Unsubscriber, similar to the `on` method.
        *         If the provided `code` does not match any registered event,
        *         the function returns nullptr.
        *         In contrast, the `on` method performs compile-time resolution
        *         and fails to compile if the specified event code does not exist.
        */ 
        template <class TEventCode, class F>
        unsubscriber_t subscribe(TEventCode code, F handler) //requires std::is_invocable<F>
        {
            std::unique_ptr<Unsubscriber> result;
            auto sel_subscription = [&](auto& single) -> bool {
                using assoc_t = std::decay_t<decltype(single)>;
                using event_sub_t = sub_by_event_code_t<assoc_t::code>;
                if (assoc_t::code == code)
                {
                    using payload_t = typename event_sub_t::type;
                    using subscriber_t = FunctorSubscription<F, payload_t>;
                    result = std::unique_ptr<Unsubscriber>{
                        new UnsubImpl<event_sub_t, subscriber_t>(single.value, std::move(handler)) };
                    return true;
                }
                return false;
                };

            std::apply([&](auto& ...single_sub) {
                static_cast<void>((... || sel_subscription(single_sub))); //lasts until `true`
                }, _subscriptions);
            return result; //may be null
        }

        /**
         * \brief Send an event with associated payload.
         *
         * \tparam EventCode Compile-time event identifier.
         *
         * \param payload Event payload.
         *
         * All current subscribers of the event will be invoked.
         * Execution order is unspecified.
         */
        template <auto EventCode>
        void send(const event_payoad_t<EventCode>& payload)
        {
            using assoc_to_ptr_t =
                std::tuple_element_t<0, utils::type_filter_t<PickEvent<EventCode>, all_evets_subscription_t> >;
            auto& single_event_sub = std::get<assoc_to_ptr_t>(_subscriptions);
            {
                std::shared_lock r_guard(single_event_sub.value->_subscribers_acc);
                raii::RefCountGuard guard(single_event_sub.value->_owned);
                for (auto& handler : single_event_sub.value->_subscribers)
                    if(!handler->call(payload))
                        break; //stop event propagation
            }

            if (single_event_sub.value->_has_unsubscribe_later)//clean delayed unsubscribers
            {
                std::unique_lock w_guard(single_event_sub.value->_unsubscribe_later_acc);
                single_event_sub.value->_unsubscribe_later.clear();
                single_event_sub.value->_has_unsubscribe_later = false;
            }
        }
    };

} //ns:OP::events

#endif // _OP_COMMON_EVENTSUPPLIER_H_
