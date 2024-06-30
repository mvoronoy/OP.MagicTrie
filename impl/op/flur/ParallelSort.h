#pragma once
#ifndef _OP_FLUR_PARALLELSORT__H_
#define _OP_FLUR_PARALLELSORT__H_

#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/OfConatiner.h>

namespace OP::flur
{
   /**
    * \brief Parallel sorting of the input source with a complexity of O(N log N). Note that additional 
    *   memory is allocated for target of sorting.
    *
    * The factory utilizes a thread pool to copy and sort the source sequence.
    * \tparam Nthreads - Number of 'steal the work' agents that implement sorting. Keep this 
    *   number low (around 1-2) and increase it only for very large volumes.
    * \tparam TThreads Thread pool type (e.g., OP::utils::ThreadPool).
    * \tparam TCompareTraits Comparator type (should return `<0`, `==0`, `>0` while comparing sequence elements).
    */
    template <size_t Nthreads, class TThreads, class TCompareTraits = full_compare_t>
    class ParallelSortFactory : FactoryBase
    {
        static_assert(
            Nthreads > 0, 
            "number of sorting threads must be greater than 0");
    
        TThreads& _pool;
        TCompareTraits _cmp;

        template <class TSrc>
        struct SharedSource
        {
            TSrc _source;
            std::mutex _access_control;
            TCompareTraits _cmp;
        };

        /** 
        *   Internal factory base that implement utility to split sorting across multiple
        *   threads
        */
        template <class TSrc>
        struct CollectSingleThreadFactory : FactoryBase
        {
            using this_t = CollectSingleThreadFactory<TSrc>;
            using shared_state_t = SharedSource<TSrc>;

            using less_t = typename CompareTraitsBase<TCompareTraits>::less_t;

            using element_t = std::decay_t<details::sequence_element_type_t<TSrc>>; //need de-reference since contains in local set
            using storage_t = std::multiset<element_t, less_t>;
            using result_sequence_t = decltype(OfContainerFactory(0, std::declval<storage_t>()).compound());

            using future_t = std::future<result_sequence_t>;

            future_t _future;
            
            CollectSingleThreadFactory(future_t future)
                : _future(std::move(future))
            {
            }

            auto compound() && 
            {
                return this->_future.get();
            }
            
            /** implement agent to 'steal the work' that picks a next item from source sequence */
            static result_sequence_t collect(std::shared_ptr<shared_state_t> source)
            {
                storage_t result;
                for(;;)
                {
                    const std::lock_guard<std::mutex> guard(source->_access_control);
                    if(source->_source.in_range())
                    {
                        result.emplace(source->_source.current());
                        source->_source.next();
                    }
                    else
                        break;
                }
                return OfContainerFactory(0, std::move(result)).compound();
            }

        };

        template <class TSrc, std::size_t... I>
        auto start_parallel_sort(TSrc&& source, std::index_sequence<I...>) const
        {
            
            using factory_t = CollectSingleThreadFactory<TSrc>;
            using shared_state_t = typename factory_t::shared_state_t;

            std::shared_ptr<shared_state_t> shared_src(
                new shared_state_t{std::move(source), std::mutex{}, _cmp});
            details::get_reference(shared_src->_source).start();

            auto make_collect = [&, shared_src](auto/*dummy*/){
                return factory_t(_pool.async(factory_t::collect, shared_src));
            };

            return MergeSortUnionFactory{
                _cmp,
                // (I) just to pack expansion
                make_collect(I)... };
        }

    public:

        constexpr ParallelSortFactory(TThreads& pool, TCompareTraits cmp = {}) noexcept
            : _pool(pool)
            , _cmp(std::move(cmp))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using sorting_sequence_t = details::sequence_type_t<
                decltype(start_parallel_sort(
                    std::move(src), std::make_index_sequence<Nthreads>{})) >;

            using proxy_sequence_t = SequenceProxy<Src, sorting_sequence_t>;
            if (src.is_sequence_ordered())
            {
                return proxy_sequence_t{ std::move(src) };
            }
            else
            {
                auto merge = start_parallel_sort(
                    std::move(src), std::make_index_sequence<Nthreads>{});

                return proxy_sequence_t{ std::move(merge).compound() };
            }
        }

    };

}  //ns:OP::flur


#endif //_OP_FLUR_PARALLELSORT__H_
