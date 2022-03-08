#include <future>
#include <chrono>
#include <thread>

#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>

#include <op/common/ftraits.h>
#include <op/common/ThreadPool.h>

#include <op/flur/OfGenerator.h>
#include <op/flur/flur.h>
#include <op/flur/QueueSrc.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;
using namespace std::chrono_literals;
// this is emulation of slow consumer
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
    OP::utils::ThreadPool tp;

    QueueSrc<int> tee;

    constexpr int i_start = 20, i_end = 114;
    auto teepipeline = src::of_iota(i_start, i_end)
        >> then::mapping([&tp](auto i) ->std::future<int> {
        return tp.async( [i]() -> int {
            std::this_thread::sleep_for(200ms);
            return i;
            })/*.share()*/;
            })
        >> then::cartesian(
            src::outer(std::ref(tee))  
            >> then::mapping([](auto i) {
                
                std::ios_base::fmtflags out_flag(std::cout.flags());
                std::cout << "\t>inner:(" << i <<")"<< std::hex << i << "\n";
                std::cout.flags(out_flag);
                return i;
            })
            >> then::repeater()
            , [&tp](std::future<int> outer, int inner)
            {
                return tp.async(
                    [](std::future<int> a, int b) -> std::tuple<int, int, int> {
                        int v = a.get();
                        return std::make_tuple(v, b, gcd(v, b));
                    }, std::move(outer), inner);
            })
        >> then::minibatch<16>(tp)
        >> then::mapping([](const auto& future)
            {
                using t_t = std::decay_t<decltype(future)>;
                return const_cast<t_t &>( future ).get();
            })
        ;
        //following array contains big ints that on 0xFFF00 bitmask provides 0
        const int big_int_mask_c = 0xFFF00;
        constexpr std::array<int, 5> big_int_basis{ 
            1+(2 << 23), 11+ (2 << 24), 13 + (2 << 25), 17 + (2 << 26), 19 + (2 << 27) };
        size_t count_out = 0;
        auto back_work = tp.async([&]() {
            for(const auto& gcd_args:teepipeline) 
            {
                count_out++;
                tresult.assert_false(big_int_mask_c & std::get<1>(gcd_args), "Wrong bigint collected");
                std::ios_base::fmtflags out_flag(std::cout.flags());
                tresult.debug() << std::hex << std::get<0>(gcd_args) << ", " << std::get<1>(gcd_args) << " = " << std::get<2>(gcd_args) << "\n";
                std::cout.flags(out_flag);
            }
        });

        for(auto i : big_int_basis)
            tee.push(i);
        tee.stop();
        back_work.wait();
        //future1.wait();
        tresult.assert_that<equals>(count_out, (i_end - i_start) * big_int_basis.size(), "Wrong num of rows produced");
}

void test_mt_exceptions(OP::utest::TestResult& tresult)
{
    auto f = std::async(std::launch::async, [] { return 0; });
    auto x1 = src::of_lazy_value([&]() {return std::move(f); });
    for (auto x0 = x1.compound(); false; x0.next())
    {
    }
    tresult.info() << "Exception of async consumption...\n";
    OP::utils::ThreadPool tp;
    auto pipeline = src::of_iota(0, 10)
        >> then::mapping([&tp](auto i) ->std::future<int> {
        return tp.async([i]() -> int {
            std::this_thread::sleep_for(200ms);
            return (i & 1) ? throw std::runtime_error("-"):i;
            })/*.share()*/;
            })
        >> then::mapping([](auto fi) {
                return fi.get();
            })
        >> then::minibatch<16>(tp)
        ;
    tresult.assert_exception<std::runtime_error>([&](){
        for(auto i : pipeline)
        {
            tresult.assert_that<equals>(0, i);//only once
        }
        });
    tresult.info() << "check exception during asyn queue consumption...\n";
    QueueSrc<std::future<int>> queue_for_pipeline2;
    auto pipeline2 = src::outer(std::ref(queue_for_pipeline2))
        >> then::mapping([](const auto& fut) { 
        return const_cast<std::future<int>&>(fut).get();
            })
        ;

    queue_for_pipeline2.push(tp.async([&]() -> int {
            return 0;
        }));
    queue_for_pipeline2.push(tp.async([&]() ->int {
            throw std::runtime_error("instead of 1");
        }));
    queue_for_pipeline2.stop();
    tresult.assert_exception<std::runtime_error>([&]() {
        for(auto i : pipeline2) {
            tresult.assert_that<equals>(0, i);//only once
            };
        });
    tresult.info() << "check async exception during 'next'...\n";
    std::atomic<int> i3{ 0 };
    auto pipeline3 = src::generator([&]()  {
        if (i3 > 0)
        {
            throw std::runtime_error("generation fail emulation");
        }
        //don't do optional of future in real code
        return
            (i3 > 3) ?
                std::optional<std::future<int>>{} : std::make_optional (tp.async([&]() {
            ++i3;
            return i3.load();
            }));
        })
        //>> then::minibatch<4>(tpool)

        >> then::mapping([](const auto& fi) {
                return const_cast<std::decay_t<decltype(fi)>&>(fi).get();
            })
    ;
    tresult.assert_exception<std::runtime_error>([&]() {
        for (auto _ : pipeline3) {}
        /*
        pipeline.for_each([&](auto i) {
            tresult.assert_that<equals>(0, i);//only once
            });
            */
        });
}
static auto module_suite = OP::utest::default_test_suite("flur.multithread")
->declare(test_Mt, "mt")
->declare(test_mt_exceptions, "exceptions")
;