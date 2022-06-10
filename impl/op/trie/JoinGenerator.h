#pragma once
#include <op/flur/Sequence.h>
#include <op/flur/typedefs.h>

template <class Trie, class OuterSequence>
struct JoinGenerator : OP::flur::OrderedSequence<typename Trie::iterator>
{
    using iterator = typename Trie::iterator;
    using base_t = OP::flur::OrderedSequence<iterator>;
    using element_t = typename base_t::element_t;

    JoinGenerator(
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
            auto diff = key_cmp(_position.key(), _prefix.current());
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
    
    template <class L, class R>
    static int key_cmp(const L& test, const R& prefix)
    {
        auto lb = std::begin(test);
        auto le = std::end(test);
        auto rb = std::begin(prefix);
        auto re = std::end(prefix);
        //do lexicographic compare to check if ever key starts with prefix 
        int diff = OP::ranges::str_lexico_comparator(lb, le, rb, re);
        if (diff > 0)
        {
            size_t delt = (lb - std::begin(test));
            if (prefix.size() <= delt)
                diff = 0;
        }
        return diff;
    }

    std::shared_ptr<Trie const> _trie;
    OuterSequence _prefix;
    iterator _position;
};

template <class Trie>
struct JoinGeneratorFactory : OP::flur::FactoryBase
{
    JoinGeneratorFactory(std::shared_ptr<Trie const> trie)
        : _trie(std::move(trie))
    {}

    template <class Src>
    constexpr auto compound(Src&& src) const /*noexcept*/
    {
        using src_container_t = OP::flur::details::sequence_type_t<Src>;
        using target_set_t = JoinGenerator<Trie, src_container_t>;
        if(!OP::flur::details::get_reference(src).is_sequence_ordered())
            throw std::runtime_error("join allowed for ordered sequence only");
        return target_set_t(_trie, std::move(src));
    }
private:
    std::shared_ptr<Trie const> _trie;
};

namespace OP::flur::then
{
    /**
    *  Produce range with all entries from Trie that start with prefixes contained in source range. Source range must be sorted.
    */
    template <class Trie>
    auto prefix_join(std::shared_ptr<Trie const> trie) noexcept
    {
        return JoinGeneratorFactory<Trie>(trie);
    }
    template <class Trie>
    auto prefix_join(std::shared_ptr<Trie> trie) noexcept
    {
        return prefix_join(std::const_pointer_cast<Trie const>(trie));
    }

}
