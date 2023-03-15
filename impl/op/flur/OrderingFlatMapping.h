#pragma once
#ifndef _OP_TRIE_ORDERINGFLATMAPPING__H_
#define _OP_TRIE_ORDERINGFLATMAPPING__H_

#include <op/common/ThreadPool.h>

#include <op/flur/Sequence.h>
#include <op/flur/FlatMapping.h>
#include <op/flur/typedefs.h>
#include <shared_mutex>

OP::utils::ThreadPool& my_global_pool();

namespace OP::flur
{
    namespace fdet = OP::flur::details;
    /**
    *   Implement sorting while flat-mapping on taken children from prfixes
    *   \tparam TCompareTraits - trait to compare keys (obviously by OP::ranges::str_lexico_comparator)
    *                           see OP::flur::CompareTraits for reference.
    */
    template <class F, class TSequence, class TCompareTraits>
    struct OrderingFlatMapping : OP::flur::OrderedSequence<
        typename OP::flur::FlatMapTraits<F, TSequence>::element_t>
    {
        using comparator_t = std::decay_t<TCompareTraits>;
        using traits_t = OP::flur::FlatMapTraits<F, TSequence>;
        using base_t = OP::flur::OrderedSequence<typename traits_t::element_t>;
        using element_t = typename base_t::element_t;

        constexpr OrderingFlatMapping(
            F applicator, TSequence&& seq, TCompareTraits cmp_traits) noexcept
            : _applicator(std::move(applicator))
            , _prefix(std::move(seq))
            , _cmp_traits(cmp_traits)
        {
        }

        void start() override 
        {
            _seq_of_seq.clear();
            _reorder_index.clear();
            for(_prefix.start(); _prefix.in_range(); _prefix.next() )
            {
                auto new_range = _applicator( _prefix.current() );
                if constexpr( std::is_base_of_v<
                    OP::flur::FactoryBase, fdet::dereference_t< decltype(new_range) >> )
                { 
                    emplace( new_range.compound() );
                }
                else
                {
                    emplace( std::move(new_range) );
                }
            }
            _reorder_index.resize(_seq_of_seq.size());
            //create default reindexer layout 
            for (size_t i = 0; i < _reorder_index.size(); ++i)
                _reorder_index[i] = i;
            //create heap
            SequenceGreaterCmp cmp_seq(_cmp_traits.greater_factory(), this->_seq_of_seq);
            std::make_heap(_reorder_index.begin(), _reorder_index.end(), cmp_seq);
            grant_smallest();
        }

        virtual element_t current() const override
        {                                           
            return smallest().current();
        }
        
        bool in_range() const override
        {
            return !_reorder_index.empty();
        }

        void next() override
        {
            smallest().next();
            //place element to heap/queue again

            collect();
            grant_smallest();
        }

    private:
        using applicator_res_t = typename traits_t::applicator_result_sequence_t;
        
        //if sequence is already std::shared_ptr don't add wrapper
        //using applicator_res_ptr = std::conditional_t<
        //    fdet::is_shared_ptr<applicator_res_t >::value,
        //    applicator_res_t,
        //    std::shared_ptr<applicator_res_t>>;

        using que_of_seq_t = std::deque<applicator_res_t>;
        /**
        * Current STL implementation of std::push_heap have no support std::move
        * operation, so to avoid compile-time error and overhead on copy
        * need to use additional indexing operation. Next vector contains index of 
        * `que_of_seq_t` 
        */
        using reorder_index_t = std::vector< size_t >;

        /** Implement comparator for std::priority_queue over TSequence (by current element) */
        struct SequenceGreaterCmp
        {
            using cmp_t = typename comparator_t::greater_t;
            
            SequenceGreaterCmp(cmp_t&& cmp, que_of_seq_t& data_container)
                : _cmp(std::move(cmp))
                , _data_container(data_container)
                {}

            /**
            * Compare 2 ranges by `current()` element 
            * Note, priority queue sorts in reverse order (greater goes first), 
            * so comparison method reverse result of key compare
            * \param TLeft, TRight - sequnces to compare
            */
            bool operator()(size_t left_idx, size_t right_idx) const
            {
                auto&& pick_left = fdet::get_reference(
                    _data_container[left_idx]).current();
                auto&& pick_right = fdet::get_reference(
                    _data_container[right_idx]).current();
                return _cmp(pick_left, pick_right);
            }

            cmp_t _cmp;
            que_of_seq_t& _data_container;
        };
        
        bool emplace(applicator_res_t&& sequence)
        {
            auto& rseq = fdet::get_reference(sequence);
            if (!rseq.is_sequence_ordered())
                throw std::runtime_error("Children sequence of OrderingFlatMapping must be ordered");
            rseq.start();
            if (!rseq.in_range())
                return false;
            _seq_of_seq.emplace_back(std::move(sequence));
            return true;
        }
        
        auto& smallest()
        {
            return fdet::get_reference(_seq_of_seq[_reorder_index.back()]);
        }

        const auto& smallest() const
        {
            return fdet::get_reference(_seq_of_seq[_reorder_index.back()]);
        }

        /** Creare sequence from range and place to queue */
        void collect() 
        {
            auto& child_ref = smallest();
            if( child_ref.in_range() )
            { //reject empty seq
                SequenceGreaterCmp cmp_seq(_cmp_traits.greater_factory(), this->_seq_of_seq);
                // new elem is at the end
                // by spec `push_heap` treate [beg, last-1] as a heap, so 
                // last elem is a subject to add
                std::push_heap(_reorder_index.begin(), _reorder_index.end(), cmp_seq);
            }
            else
            {
                //No erase from _seq_of_seq, only index
                _reorder_index.pop_back();
            }
        }

        /** Move smallest sequence the way it available as 'back()' */
        void grant_smallest()
        {
            if (!_reorder_index.empty())
            {//make `back()` smallest referenced element
                SequenceGreaterCmp cmp_seq(_cmp_traits.greater_factory(), this->_seq_of_seq);
                std::pop_heap(_reorder_index.begin(), _reorder_index.end(), cmp_seq); // moves the smallest to the end
            }
        }
            
        F _applicator;
        TSequence _prefix;
        que_of_seq_t _seq_of_seq; 
        reorder_index_t _reorder_index;
        TCompareTraits _cmp_traits;
    };

    template <class F, class TCompareTraits>
    struct OrderingFlatMappingFactory : OP::flur::FactoryBase
    {
        using applicator_t = std::decay_t<F>;

        constexpr OrderingFlatMappingFactory(F&& applicator, TCompareTraits&& cmp) noexcept
            : _applicator(std::forward<F>(applicator))
            , _cmp(std::forward<TCompareTraits>(cmp)) 
        {}

        template <class Src>
        constexpr auto compound(Src&& src) const /*noexcept*/
        {
            using src_container_t = std::decay_t<Src>;
            using applicator_result_t = 
                std::decay_t<decltype(_applicator(fdet::get_reference(src).current()))>;

            using target_set_t = OrderingFlatMapping<applicator_t, src_container_t, TCompareTraits>;
            return target_set_t(_applicator, std::move(src), _cmp);
        }
    private:
        applicator_t _applicator;
        TCompareTraits _cmp;
    };

}//ns: OP::flur



#endif //_OP_TRIE_ORDERINGFLATMAPPING__H_
