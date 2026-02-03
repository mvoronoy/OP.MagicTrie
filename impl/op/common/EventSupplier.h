#pragma once

#ifndef _OP_COMMON_EVENTSUPPLIER_H_
#define _OP_COMMON_EVENTSUPPLIER_H_

#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>

namespace OP::common
{
    
    template <class C>
    struct EventSupplier;

    /** Plain structure to support unsubscription from events */
    template <class C>
    struct EventUnsubscriber
    {
        static_assert(std::is_enum<C>::value || std::is_integral<C>::value,
            "Event code must be enum or integral type");
        friend EventSupplier<C>;
        using event_code_t = C;
        using unsubscribe_order_t = std::uint32_t;
        using this_t = EventUnsubscriber<C>;

        constexpr event_code_t event_code() const noexcept
        {
            return _code;
        }

    private:
        constexpr EventUnsubscriber(
            event_code_t code, unsubscribe_order_t order) noexcept
            : _code(code)
            , _order(order)
        {}
        event_code_t _code;
        unsubscribe_order_t _order;

        friend inline bool operator == (const this_t& left, const this_t& right)
        {
            return left._code == right._code && left._order == right._order;
        }
        friend inline bool operator != (const this_t& left, const this_t& right)
        {
            return left._code != right._code || left._order != right._order;
        }
        friend inline bool operator < (const this_t& left, const this_t& right)
        {
            return left._code < right._code || (left._code == right._code && left._order < right._order);
        }
        friend inline bool operator > (const this_t& left, const this_t& right)
        {
            return left._code > right._code || (left._code == right._code && left._order > right._order);
        }
    };

    /** Simple RAII guard wrapper that allows aggregate many EventUnsubscriber to unsubscribe multiple subscribers */
    template <class C>
    class UnsubGuard
    {
        using event_code_t = C;
    public:
        using this_t = UnsubGuard<event_code_t>;
        using unsubscriber_t = EventUnsubscriber<event_code_t>;
        using supplier_t = EventSupplier<event_code_t>;
            
        explicit UnsubGuard(supplier_t& supplier) noexcept
            : _supplier(supplier)
        {}
            
        UnsubGuard(supplier_t& supplier, std::initializer_list<unsubscriber_t> unsub_set)
            : _supplier(supplier)
            , _unsub(std::move(unsub_set))
        {}

        ~UnsubGuard()
        {
            unsubscribe_all();
        }

        [[nodiscard]] bool empty() const
        {
            return _unsub.empty();
        }

        void unsubscribe_all()
        {
            for(auto i = _unsub.rbegin(); i != _unsub.rend(); ++i) //release in reverse order
                _supplier.unsubscribe(*i);
            _unsub.clear();
        }

        this_t& append(unsubscriber_t u)
        {
            _unsub.emplace_back(std::move(u));
            return *this;
        }
            
        friend inline this_t& operator << (this_t& target, unsubscriber_t u)
        {
            return target.append(std::move(u));
        }
    private:
        supplier_t& _supplier;
        std::vector<unsubscriber_t> _unsub;
    };

    /** Represent base event,
    * \tparam C some enumeration or integral data type to distinguish event.
    * The payload may be added by inheritance to introduce expected attributes.
    */
    template <class C>
    struct Event
    {
        static_assert(std::is_enum<C>::value || std::is_integral<C>::value,
            "Event code must be enum or integral type");
        using event_code_t = C;

        event_code_t code() const
        {
            return _code;
        }

    protected:
        Event(event_code_t event)
            : _code(event)
        {
        }

    private:
        event_code_t _code;
    };


    /** Provide implmentation of Event with attached payload
    \tparam T any payload data propagated with event
    \tparam C enum or uint32
    \tparam code - associated constant with specific payload
    */
    template <class T, class C, C c>
    struct EventImpl : public Event<C>
    {
        using base_t = Event<C>;
        static constexpr C code = c;
        EventImpl(T t)
            : base_t(code)
            , _payload(std::move(t))
        {}

        T _payload;
    };

    template <class C>
    struct EventSupplier
    {
        using event_code_t = C;
        using unsubscriber_t = EventUnsubscriber<event_code_t>;
        using base_event_t = Event<event_code_t>;
        using action_t = std::function<void(const base_event_t&)>;

        EventSupplier()
        {
        }

        /** Register action handler with specific event- code
        * @return some value that may be used to unsubscribe later. If you don't need unsubscribe functionality - just 
        * ignore this value. For multiple unsubscribers review UnsubGuard and #unsub_guard
        */
        unsubscriber_t subscribe(event_code_t event, action_t&& action)
        {
            //_CONTATA5_EVENT_LOGGING_TRACE(<< "Subscribe on:'" << std::to_string(event) << "'");
            auto subscriber_id = generate_unsubscriber(event);
            register_handle(event, subscriber_id, std::forward<action_t>(action));
            return subscriber_id;
        }

        /** Send specific event to all subscribers */
        void send(event_code_t event, const base_event_t& param) const
        {
            using namespace std::string_literals;
            //_CONTATA5_EVENT_LOGGING_TRACE(<< "Send event:'" << std::to_string(event) << "'");
            //set the tag so all consumers can see the root cause
            //_CONTATA5_EVENT_LOGGING_TAG("event:"s + std::to_string(event)); 

            auto range = _event2action.equal_range(event);
            for (auto it = range.first; it != range.second; ++it)
            {
                it->second->second(param);
            }
        }

        /** Remove action handler previously registered with `subscribe` */
        bool unsubscribe(unsubscriber_t un)
        {
            //_CONTATA5_EVENT_LOGGING_TRACE(<< "Unsubscribe from: '" << std::to_string(un.event_code()) << "'");
            auto found = _all_actions.find(un);
            if (found == _all_actions.end())
                return false;
            //extract event_code_t associated with unique_id
            auto event_code = un ._code;
            //wipe from event2action
            auto range = _event2action.equal_range(event_code);
            for (auto it = range.first; it != range.second; ++it)
            { //iterate through multiple values to find that with unique number
                if (it->second == found)
                {
                    _event2action.erase(it);
                    break;
                }
            }
            _all_actions.erase(found);
            return true;
        }

        UnsubGuard<event_code_t> unsub_guard()
        {
            return UnsubGuard< event_code_t >(*this);
        }

        template <class T, class... Args>
        T event(Args&&... args)
        {
            return T{ *this, std::forward<Args>(args)... };
        }


    private:
        unsubscriber_t generate_unsubscriber(event_code_t event)
        {
            return unsubscriber_t{ event, _subscriber_numbering++ };
        }
            
        void register_handle(event_code_t event, unsubscriber_t unsub_id, std::function<void(const base_event_t&)>&& action)
        {
            auto ins_res = _all_actions.emplace(std::make_pair(unsub_id, std::move(action)));
            _event2action.emplace(std::make_pair(event, ins_res.first));
        }

        //end-to-end numbering, where each "subscribe" get unique key to allow unique identify unsubscribe routine later
        typename unsubscriber_t::unsubscribe_order_t _subscriber_numbering = 1;

        // unsubscribers associated with unique number, map used as iterator-stable container, Worst case for unsubscribe - O(lnN)
        using unsubscribe_map_t = std::map<unsubscriber_t, action_t>;
        unsubscribe_map_t _all_actions;

        //multimap event-code to all registered actions
        using event2action_t = std::unordered_multimap<event_code_t, typename unsubscribe_map_t::iterator>;
        event2action_t _event2action;
    };

    template <class C, class ... Ingredient >
    struct EventSupplierMixer : public virtual EventSupplier<C>, public Ingredient ...
    {
        EventSupplierMixer()
            : EventSupplier<C>(), Ingredient(*this) ...
        {}
    };

    template <class C>
    struct EventIngredient
    {
        using event_code_t = C;
        using unsubscriber_t = EventUnsubscriber<event_code_t>;
        using core_supplier_t = EventSupplier<C>;
        /**When added to EventSupplierMixer allows associate event code with payload data structure. As result
        you don't need manually override Event class to provide payload. Instead use just your domain specific
        data object. For example:\code
            //declare some payload
            struct P1{ int order; double amount; }
            //declare another payload
            struct P2 {float x, y; }
            // declare domain events
            enum class DDEvents
            {
                kind1,
                kind2,
                kind3
            };

            //simplified Ingredient namespace
            using dd_ing_t = Ingredient<DDEvents>;
            //Now define EventSupplier with strict code-to-type association
            using ev_supplier_t = EventSupplierMixer<
                DDEvents,
                dd_ing_t::SupportPayload<
                    Assoc<DDEvents::kind1, P1>, //specify that code `kind1` associated with data P1
                    Assoc<DDEvents::kind2, P2>, //another type declaration
                    Assoc<DDEvents::kind3, P1> //Same type P1 may be used for several codes
                >
            >;
            // ... Later:
            ev_supplier_t ev_supplier;
            //subscribe action:
            ev_supplier.on(DDEvents::kind1, [](const P1& arg){ //strongly typed P1
                std::cout << "amount = " << arg.amount << "\n"
            });
            // send an event
            ev_supplier.event<DDEvents::kind1>(P1{5, 57.75});
        \endcode
        */
        template <class A1, class ... AssocPairs>
        class SupportPayload: public virtual EventSupplier<C>
        {
            /** Allows resolve type of event payload associated with specified code */
            template <C c, class ... Ts > struct get_type_by_c;

            template <C c, class T, class ... Ts > struct get_type_by_c<c, T, Ts...>
            {
                using type = typename std::conditional<
                    T::code == c,
                    typename T::type,
                    typename get_type_by_c<c, Ts ...>::type >::type;
            };

            template <C c > struct get_type_by_c<c>
            {
                using type = void;
            };
            using base_event_t = typename core_supplier_t::base_event_t;
            using action_t = typename core_supplier_t::action_t;
            std::function< unsubscriber_t(event_code_t, action_t&&) > _do_subscribe;
            std::function< void(event_code_t, const base_event_t&) > _do_send;

        public:
            using base_supplier_t = EventSupplier<C>;
            template <C c>
            using payload_t = typename get_type_by_c<c, A1, AssocPairs ...>::type;

            template <C c>
            using event_t = EventImpl<payload_t<c>, C, c>;

            template <class Context>
            SupportPayload(Context& context)
            {
            }

            /**Subscribe `action` to event `c` */
            template <C c>
            unsubscriber_t on(std::function<void(const payload_t<c>&)> action)
            {
                return base_supplier_t::subscribe(c, [action = std::move(action)](const base_event_t& event){
                    action(static_cast<const event_t<c>&>(event)._payload);
                });
            }

            /** Create and broadcast event with payload */
            template <C c>
            void send_event(payload_t<c> data) const
            {
                /*_do_send(c, event_t<c>(std::move(data)));
                */
                base_supplier_t::send(c, event_t<c>(std::move(data)));
            }

            template <C c>
            void send(payload_t<c> data) const
            {
                base_supplier_t::send(c, event_t<c>(std::move(data)));
            }
            using base_supplier_t::send;
        };
    };//EventIngredient
} //ns:OP::common

#endif // _OP_COMMON_EVENTSUPPLIER_H_
