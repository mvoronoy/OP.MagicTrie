#include <future>
#include <chrono>
#include <thread>

#include <map>
#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>

#include <op/common/ftraits.h>

#include <op/flur/OfGenerator.h>
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
    constexpr SlowSrc(int limit, int start = 0) noexcept
        : _limit(limit)
        , _start(start)
        , _current(start)
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

namespace beam
{
    template <class T>
    constexpr auto transient(T t) noexcept
    {
        //return TransientFactory<T>(std::move(t));
        return SimpleFactory<T, T>(std::move(t));
    }
}



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
    
    constexpr int i_start = 20, i_end = 114;
    auto teepipeline = src::of_iota(i_start, i_end)
        >> then::mapping([](auto i) {
        return std::async(std::launch::async, [=]() {
            std::this_thread::sleep_for(200ms);
            return i;
            });
            })
        >> then::cartesian(
            make_lazy_range(beam::transient(std::ref(tee))) >> then::repeater(),
            [](auto outer, auto inner)
            {
                return std::async(std::launch::async, [](auto& a, int b) {
                    auto v = a.get();
                    return std::make_tuple(v, b, gcd(v, b));
                    }, std::move(outer), std::move(inner));
            })
        >> then::minibatch<16>()
        >> then::mapping([](const auto& future)
            {
                using t_t = std::decay_t<decltype(future)>;
                return const_cast<t_t &>( future ).get();
            })
        ;

        /*auto dt = make_lazy_range(beam::transient(std::ref(tee))) >> beam::repeater();
        auto future1 = std::async(std::launch::async, [&]() {dt.for_each([](const auto& i1) { std::cout << "i1:" << i1 << "\n"; });
        dt.for_each([](const auto& i2) { std::cout << "i2:" << i2 << "\n"; }); });*/

        auto back_work = std::async(std::launch::async, [&]() {
            teepipeline.for_each([](const auto& gcd_args) {
                std::cout << std::get<0>(gcd_args) << ", " << std::get<1>(gcd_args) << " = " << std::get<2>(gcd_args) << "\n";
                });
            });

        tee.push(1 + (2 << 23));
        tee.push(11 + (2 << 24));
        tee.push(13 + (2 << 25));
        tee.push(17 + (2 << 26));
        tee.push(19 + (2 << 27));
        tee.stop();
        back_work.wait();
        //future1.wait();
}


static auto module_suite = OP::utest::default_test_suite("flur.mt")
->declare(test_Mt, "mt")
;