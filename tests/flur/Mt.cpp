#include <future>
#include <chrono>
#include <thread>

#include <map>
#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>
#include <op/flur/Reducer.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;
using namespace std::chrono_literals;

struct SlowSrc : OP::flur::Sequence<int>
{
    int _current;
    int _limit;
    int _start;
    SlowSrc(int limit, int start = 0)
        : _limit(limit)
        , _start(start)
    {
    }
    /** Start iteration from the beginning. If iteration was already in progress it resets.  */
    virtual void start()
    {
        std::this_thread::sleep_for(200ms);
        _current = _start;
    }
    /** Check if Sequence is in valid position and may call `next` safely */
    virtual bool in_range() const
    {
        return _current < _limit;
    }
    /** Return current item */
    virtual element_t current() const
    {
        return _current;
    }
    /** Position iterable to the next step */
    virtual void next()
    {
        std::this_thread::sleep_for(200ms);
        ++_current;
    }
};

template <class T, class F, bool ordered>
struct Generator : std::conditional_t< ordered,
    OrderedSequence<const T&>,
    Sequence<const T&>
>
{
    using base_t = std::conditional_t< ordered,
        OrderedSequence<const T&>,
        Sequence<const T&>
    >;


    constexpr Generator(F f) noexcept
        : _generator(std::move(f)) {}
    /** Start iteration from the beginning. If iteration was already in progress it resets.  */
    virtual void start()
    {
        _current = std::move(_generator());
    }
    /** Check if Sequence is in valid position and may call `next` safely */
    virtual bool in_range() const
    {
        return _current.has_value();
    }
    /** Return current item */
    virtual const T& current() const
    {
        return _current.value();
    }
    /** Position iterable to the next step */
    virtual void next()
    {
        _current = std::move(_generator());
    }
private:
    std::optional<T> _current;
    F _generator;
};
/**
* \tparam F - must produce std::optional<?>
* \tparam ordered - if generator produce ordered sequence
*/
template <class F, bool ordered>
class GeneratorFactory : FactoryBase
{
    static inline auto decl_r(F a)
    {
        return a();
    }
    F _gen;
public:
    using result_t = decltype(decl_r(std::declval<F>()));
    static_assert(OP::utils::is_generic<result_t, std::optional>::value,
        "Generator must produce std::optional<?> value");

    using element_t = typename result_t::value_type;

    GeneratorFactory(F&& f)
        : _gen(std::move(f))
    {
    }

    constexpr auto compound() const noexcept
    {
        return Generator<element_t, F, ordered>(_gen);
    }
};
template <class F>
constexpr auto generator(F&& f) noexcept
{
    return GeneratorFactory<F, false>(std::move(f));
}

template <class Base, class Src, class Container = std::vector<typename Base::element_t>>
class Repeater : Base
{
    using base_t = Base;
    using element_t = typename base_t::element_t;
    using this_t = Repeater<Base, Container>;
    using iterator_t = typename Container::const_iterator;
    Src _src;
    Container _container;
    size_t _latch = 0;
    size_t _current = -1;
    void peek()
    {
        if (OP::flur::details::get_reference(_src).in_range())
            _container.emplace_back(
                std::move(
                    OP::flur::details::get_reference(_src).current()
                )
            );
    }
public:
    constexpr Repeater(Src&& src)
        :_src(std::move(src))
    {}
    virtual void start()
    {
        if (++_latch == 1)
        {
            OP::flur::details::get_reference(_src).start();
            peek();
        }
        _current = 0;
    }

    virtual bool in_range() const
    {
        return _current < _container.size();
    }
    virtual element_t current() const
    {
        return _container[_current];
    }
    virtual void next()
    {
        if (_latch == 1)
        {
            OP::flur::details::get_reference(_src).next();
            peek();
        }
        ++_current;
    }

};
using namespace OP::flur;

struct RepeaterFactory : FactoryBase
{
    template <class Src>
    constexpr auto compound(Src&& src) const noexcept
    {
        using target_container_t = std::decay_t<decltype(OP::flur::details::get_reference(src))>;
        using src_container_t = OP::flur::details::unpack_t<Src>;
        using base_t = std::conditional_t< (target_container_t::ordered_c),
            OrderedSequence<typename target_container_t::element_t>,
            Sequence<typename target_container_t::element_t>
        >;
        return Repeater<base_t, Src>(std::move(src));
    }

};
namespace beam
{
    /** */
    constexpr auto repeater() noexcept
    {
        return RepeaterFactory();
    }

    template <class T>
    struct TransientFactory : FactoryBase
    {
        T _t;
        constexpr TransientFactory(T t)
            : _t(std::move(t))
        {}
        constexpr const auto& compound() const noexcept
        {
            return _t;
        }
    };
    template <class T>
    constexpr auto transient(T t) noexcept
    {
        return TransientFactory<T>(std::move(t));
    }
}


template <class T, class Container = std::deque<T>>
class MTRepeater : OP::flur::OfContainer<Container, OP::flur::Sequence<T>>
{
    using base_of_base_t = OP::flur::Sequence<T>;
    using base_t = OP::flur::OfContainer<Container, OP::flur::Sequence<T>>;
    using element_t = typename base_t::element_t;
    using this_t = MTRepeater<T, Container>;
    using guard_t = std::unique_lock<std::mutex>;
    using position_t = typename Container::iterator;

    Container _container;
    mutable std::mutex _m;
    mutable std::condition_variable _access_condition;
    std::atomic_bool _stop = false;
    position_t _current;

    struct MethodImpl
    {
        void (*_start_method)(this_t&);
        bool (*_in_range_method)(const this_t&);
        element_t(*_current_method)(const this_t&);
        void (*_next_method)(this_t&);
    };
    //std::atomic< MethodImpl*> _current_handler;
    std::atomic< bool > _current_handler = false;

    void collect_start()
    {
        _stop = false;
        _current = _container.begin();
    }
    /** Check if Sequence is in valid position and may call `next` safely */
    bool collect_in_range() const
    {
        guard_t g(_m);
        if (_current == _container.end())
        {
            _access_condition.wait(g, [this] {return _stop || _current != _container.end(); });
            return _current != _container.end();
        }
        else
            return true;
    }
    /** Return current item */
    virtual element_t collect_current() const
    {
        guard_t g(_m);
        if (_current == _container.end())
        {
            _access_condition.wait(g, [this] {return _stop || _current != _container.end(); });
            return *_current;
        }
        else
            return *_current;
    }
    /** Position iterable to the next step */
    void collect_next()
    {
        guard_t g(_m);
        if (_current == _container.end())
        {
            _access_condition.wait(g, [this] {return _stop || _current != _container.end(); });
            ++_current;
        }
        else
            ++_current;
    }
    void repeat_next()
    {
        ++_current;
    }
    inline static auto _collect = std::make_tuple(
        &this_t::collect_start,
        &this_t::collect_in_range,
        &this_t::collect_current,
        &this_t::collect_next
    );
    inline static auto _repeat = std::make_tuple(
        &base_t::start,
        &base_t::in_range,
        &base_t::current,
        &base_t::next
    );
    template <size_t method_i, class Vt, class ... Tx>
    auto call(const Vt& vt, Tx&& ...tx)
    {
        return (this->*std::get<method_i>(vt))(std::forward<Tx>(tx)...);
    }
    template <size_t method_i, class Vt, class ... Tx>
    auto call(const Vt& vt, Tx&& ...tx) const
    {
        return (this->*std::get<method_i>(vt))(std::forward<Tx>(tx)...);
    }
public:
    MTRepeater()
        : base_t(std::cref(_container))
        , _current_handler(false)
    {
    }
    void push(int v)
    {
        if (_stop)
            throw std::logic_error("push data after 'stop' has been invoked");
        guard_t g(_m);
        _container.push_back(v);
        g.unlock();
        _access_condition.notify_one();
    }
    void stop()
    {
        _stop.store(true);
        _access_condition.notify_one();
        //note - no reset iterator since something can be not-consumed in the _container
        _current_handler.store(&_repeat);
    }
    /** Start iteration from the beginning. If iteration was already in progress it resets.  */
    virtual void start()
    {
        if (_current_handler)
            call<0>(_repeat);
        else
            call<0>(_collect);
    }
    /** Check if Sequence is in valid position and may call `next` safely */
    virtual bool in_range() const
    {
        if (_current_handler)
            return call<1>(_repeat);
        else
            return call<1>(_collect);
    }
    /** Return current item */
    virtual element_t current() const
    {
        if (_current_handler)
            return call<2>(_repeat);
        else
            return call<2>(_collect);
    }
    /** Position iterable to the next step */
    virtual void next()
    {
        //_current_handler->_next_method();
        if (_current_handler)
            call<3>(_repeat);
        else
            call<3>(_collect);
    }
};

struct QueueSrc : OP::flur::Sequence<int>
{
    std::deque<int> _queue;

    mutable std::mutex _m;
    using guard_t = std::unique_lock<std::mutex>;
    mutable std::condition_variable _access_condition;
    std::atomic_bool _stop = false;

    QueueSrc()
    {
    }
    void push(int v)
    {
        if (_stop)
            throw std::logic_error("push data after 'stop' has been invoked");

        guard_t g(_m);
        _queue.push_back(v);
        g.unlock();
        _access_condition.notify_one();
    }
    void stop()
    {
        _stop.store(true);
        _access_condition.notify_one();
    }
    /** Start iteration from the beginning. If iteration was already in progress it resets.  */
    virtual void start()
    {
    }
    /** Check if Sequence is in valid position and may call `next` safely */
    virtual bool in_range() const
    {
        guard_t g(_m);
        if (_queue.empty())
        {
            _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
            return !_queue.empty();
        }
        else
            return true;
    }
    /** Return current item */
    virtual element_t current() const
    {
        guard_t g(_m);
        if (_queue.empty())
        {
            _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
            return _queue.front();
        }
        else
            return _queue.front();
    }
    /** Position iterable to the next step */
    virtual void next()
    {
        guard_t g(_m);
        if (_queue.empty())
        {
            _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
            _queue.pop_front();
        }
        else
            _queue.pop_front();
    }
};

struct ya2
{
    using container_t = std::vector<int>;
    container_t _container;
    std::mutex _acc;

};
/** Find "Greatest Common Divisor" by Euclidian algorithm as very slow algorithm */
int gcd(int a, int b)
{
    while (b != 0)
    {
        auto t = b;
        b = a % b;
        a = t;
    }
    return a;
}


template <class Src, size_t N>
struct Minibatch : std::conditional_t< Src::ordered_c,
    OrderedSequence< const typename Src::element_t& >,
    Sequence< const typename Src::element_t& >
>
{
    static_assert(N > 2, "to small N for drain buffer");

    using base_t = std::conditional_t< Src::ordered_c,
        OrderedSequence< const typename Src::element_t& >,
        Sequence< const typename Src::element_t& >
    >;
    using src_element_t = typename Src::element_t;
    using element_t = typename base_t::element_t;
    using storage_t = std::array< src_element_t, N>;
    Src _src;
    storage_t _batch;
    std::atomic_size_t _r_current, _w_current;
    //using background_t = std::future<void>;
    //std::aligned_storage<sizeof(background_t), alignof(background_t)>::type _data_background;  
    bool _has_back = false;
    //_background;

    constexpr Minibatch(Src&& src) noexcept
        :_src(std::move(src))
    {
    }

    void drain(size_t lim)
    {
        for (; lim && _src.in_range(); _src.next(), --lim)
        {
            _batch[(_w_current % N)] = std::move(_src.current());
            ++_w_current;
        }
    }
    void async_drain(size_t lim)
    {
        /*
        if (_background.valid())
            _background = std::async(std::launch::async, [b = std::move(_background)](size_t lim2){
                b.wait();
                drain(lim2);
            }, lim);
        else
            _background = std::async(std::launch::async, [](size_t lim2) {
            drain(lim2);
                }, lim);
        */
    }
    virtual void start()
    {
        _r_current = _w_current = 0;
        _src.start();
        if (_src.in_range())
        {
            _batch[_w_current++] = std::move(_src.current());
            async_drain(N - 1);
        }
    }
    /** Check if Sequence is in valid position and may call `next` safely */
    virtual bool in_range() const
    {
        return (_r_current < _w_current) || _src.in_range(); //potential issue: t0{r == w}, t1{drain, now r < w}, t1{src_in_range = false, but r < w}
    }
    /** Return current item */
    virtual const element_t& current() const
    {
        return _batch[_r_current % N];
    }
    /** Position iterable to the next step */
    virtual void next()
    {
        if (_r_current < _w_current)
            ++_r_current;
        else
        {
            assert(_src.in_range());

        }
    }
};

template <size_t N>
struct MinibatchFactory : FactoryBase
{
    template <class Src>
    constexpr auto compound(Src&& src) const noexcept
    {
        return Minibatch<Src, N>(std::move(src));
    }
};

template <size_t N>
constexpr auto minibatch() noexcept
{
    return MinibatchFactory<N>{};
}

void test_Mt(OP::utest::TestResult& tresult)
{

    //(src::of_iota(20, 114) >> then::mapping([](auto i) {
    //        return std::async(std::launch::async, [=]() {
    //            std::this_thread::sleep_for(200ms);
    //            return i;
    //            });
    //    })).for_each([](auto& i) {
    //        std::cout << "inp>" << i.get() << "\n";
    //        });
    QueueSrc tee;
    using demo_tuple = std::tuple<int, int, std::future<int>>;
    
    auto teepipeline = src::of_iota(20, 114)
        >> then::mapping([](auto i) {
        return std::async(std::launch::async, [=]() {
            std::this_thread::sleep_for(200ms);
            return i;
            });
            })
        >> then::cartesian(
            make_lazy_range(beam::transient(std::ref(tee))) >> beam::repeater(),
            [](auto outer, auto inner)
            {
                return std::async(std::launch::async, [](auto& a, int b) {
                    auto v = a.get();
                    return std::make_tuple(v, b, gcd(v, b));
                    }, std::move(outer), std::move(inner));
            })
        //>> minibatch<5>()
        >> then::mapping([](auto future)
            {
                return future.get();
            })
        ;

        /*auto dt = make_lazy_range(beam::transient(std::ref(tee))) >> beam::repeater();
        auto future1 = std::async(std::launch::async, [&]() {dt.for_each([](const auto& i1) { std::cout << "i1:" << i1 << "\n"; });
        dt.for_each([](const auto& i2) { std::cout << "i2:" << i2 << "\n"; }); });*/

        auto back_work = std::async(std::launch::async, [&]() {
            teepipeline.for_each([](const auto& gcd) {
                std::cout << std::get<0>(gcd) << ", " << std::get<1>(gcd) << " = " << std::get<2>(gcd) << "\n";
                });
            });

        tee.push(1 + (2 << 23));
        tee.push(11 + (2 << 24));
        tee.push(13 + (2 << 25));
        tee.push(17 + (2 << 26));
        tee.push(19 + (2 << 26));
        tee.stop();
        back_work.wait();
        //future1.wait();
}


static auto module_suite = OP::utest::default_test_suite("flur.mt")
->declare(test_Mt, "mt")
;