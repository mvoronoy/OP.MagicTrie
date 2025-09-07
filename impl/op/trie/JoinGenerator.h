#pragma once
#ifndef _OP_TRIE_JoinOrderedSequenceWithTrie__H_
#define _OP_TRIE_JoinOrderedSequenceWithTrie__H_

#include <op/flur/Sequence.h>
#include <op/flur/Conditional.h>
#include <op/flur/typedefs.h>

namespace opfd = OP::flur::details;
namespace details
{
    template <class L, class R>
    static inline int prefixed_key_cmp(const L& test, const R& prefix)
    {
        auto lb = std::begin(test);
        auto le = std::end(test);
        auto rb = std::begin(prefix);
        auto re = std::end(prefix);
        //do lexicographic compare to check if ever key starts with prefix 
        int diff = OP::common::str_lexico_comparator(lb, le, rb, re);
        if (diff > 0)
        { // approve the case when checking prefix is shorter than found entry 
            size_t delta = (lb - std::begin(test));
            if (prefix.size() <= delta)
                diff = 0;
        }
        return diff;
    }


}//ns:details
template <class Trie, class OuterSequence>
struct JoinOrderedSequenceWithTrie : OP::flur::OrderedSequence<typename Trie::iterator>
{
    using iterator = typename Trie::iterator;
    using base_t = OP::flur::OrderedSequence<iterator>;
    using element_t = typename base_t::element_t;

    JoinOrderedSequenceWithTrie(
        std::shared_ptr<Trie const> trie_ptr,
        OuterSequence && seq) noexcept
        : _trie(trie_ptr)
        , _prefix(std::move(seq))
    {
    }

    void start() 
    {
        _prefix.start();
        if( _prefix.in_range() )
        {
            _position = _trie->lower_bound(_prefix.current());
            seek();
        }
    }

    virtual element_t current() const
    {
        return _position;
    }
    bool in_range() const
    {
        return !_position.is_end() && _prefix.in_range();
    }
    void next()
    {
        _trie->next(_position);
        seek();
    }
private:
    void seek() 
    {
        bool left_succeed = _trie->in_range(_position);
        bool right_succeed = _prefix.in_range();
        while (left_succeed && right_succeed)
        {
            auto diff = ::details::prefixed_key_cmp(_position.key(), _prefix.current());
            if (diff < 0) 
            {
                _trie->next_lower_bound_of(_position, _prefix.current());
                left_succeed = _trie->in_range(_position);
            }
            else 
            {
                if (diff == 0) 
                {
                    return;
                }
                _prefix.next();
                right_succeed = _prefix.in_range();
            }
        }
    }
    
    std::shared_ptr<Trie const> _trie;
    OuterSequence _prefix;
    iterator _position;
};


/** Use the fact that Trie has almost O(1) performance on check presence */
template <class Trie, class OuterSequence>
struct UnorderedJoinSequenceWithTrie : OP::flur::Sequence<typename Trie::iterator>
{
    using iterator = typename Trie::iterator;
    using base_t = OP::flur::Sequence<iterator>;
    using element_t = typename base_t::element_t;

    UnorderedJoinSequenceWithTrie(
        std::shared_ptr<Trie const> trie_ptr,
        OuterSequence&& seq) noexcept
        : _trie(trie_ptr)
        , _prefix(std::move(seq))
    {
    }

    void start()
    {
        opfd::get_reference(_prefix).start();
        _position = _trie->end();
        seek();
    }

    virtual element_t current() const
    {
        return _position;
    }

    bool in_range() const
    {
        return !_position.is_end() 
            && opfd::get_reference(_prefix).in_range();
    }

    void next()
    {
        if(_position.is_end()) 
            opfd::get_reference(_prefix).next();
        seek();
    }

private:

    template<class TKey>
    bool seek_trie(const TKey& key)
    {
        auto kb = std::begin(key), ke = std::end(key);
        if (_position.is_end())
            _position = _trie->prefixed_begin(kb, ke);
        else
            _trie->next_lower_bound_of(_position, key);

        if (_position.is_end())
            return false;
        if (0 == ::details::prefixed_key_cmp(_position.key(), key))
            return true;
        //prefix doesn't match to expected anymore, reset trie iterator
        _position.clear();
        return false;
    }

    void seek()
    {
        for (auto& src_ref = opfd::get_reference(_prefix);
            src_ref.in_range();
            src_ref.next()
            )
        {
            if (seek_trie(src_ref.current()))
                return;
        }
    }

    std::shared_ptr<Trie const> _trie;
    OuterSequence _prefix;
    iterator _position;
};

template <class Trie>
struct JoinOrderedSequenceWithTrieFactory : OP::flur::FactoryBase
{
    JoinOrderedSequenceWithTrieFactory(std::shared_ptr<Trie const> trie)
        : _trie(std::move(trie))
    {}

    template <class Src>
    constexpr auto compound(Src&& src) const /*noexcept*/
    {
        using src_sequence_t = opfd::sequence_type_t<Src>;
        using ordered_seq_t = JoinOrderedSequenceWithTrie<Trie, src_sequence_t>;
        using unordered_seq_t = UnorderedJoinSequenceWithTrie<Trie, src_sequence_t>;

        using target_sequence_t = OP::flur::SequenceProxy<
            ordered_seq_t, unordered_seq_t >;


        if (opfd::get_reference(src).is_sequence_ordered())
        {
            return target_sequence_t(ordered_seq_t(_trie, std::move(src)));
        }
        else
        {
            return target_sequence_t(unordered_seq_t(_trie, std::move(src)));
        }
    }

private:
    std::shared_ptr<Trie const> _trie;
};

namespace OP::flur::then
{
    /**
    *  Produce range with all entries from Trie that start with prefixes contained in source range. 
    *   In case when source range is sorted result range is sorted as well, but for unordered result is unordered.
    */
    template <class Trie>
    auto prefix_join(std::shared_ptr<Trie const> trie) noexcept
    {
        return JoinOrderedSequenceWithTrieFactory<Trie>(trie);
    }
    template <class Trie>
    auto prefix_join(std::shared_ptr<Trie> trie) noexcept
    {
        return prefix_join(std::const_pointer_cast<Trie const>(trie));
    }

} //ns: OP::flur::then
#endif //_OP_TRIE_JoinOrderedSequenceWithTrie__H_
