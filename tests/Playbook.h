#pragma once

//*************************************************
template <class TTrie>
struct GenArrayBase
{
    using payload_t = Payload<TTrie>;
    virtual size_t size() const = 0;
    virtual bool call(size_t i, const TTrie& trie, payload_t& payload, bool first) = 0;
};

template <class TTrie, class TTup>
struct GenArray : public GenArrayBase<TTrie>
{
    GenArray(TTup && args) :
        _generators(std::move(args))
    {}
    size_t size() const override
    {
        return std::tuple_size<TTup> ::value;
    }
    
    bool call(size_t i, const TTrie& trie, payload_t& payload, bool first)
    {
        switch(i)
        {
        case 0:
            return std::get<0>(_generators)(trie, payload, first);
        case 1:
            return std::get<1>(_generators)(trie, payload, first);
        case 2:
            return std::get<2>(_generators)(trie, payload, first);
        case 3:
            return std::get<3>(_generators)(trie, payload, first);
/*        case 4:
            return std::get<4>(_generators)(trie, payload, first);
        case 5:
            return std::get<5>(_generators)(trie, payload, first);
        case 6:
            return std::get<6>(_generators)(trie, payload, first);*/
        }
        throw std::exception("!!!!!!!");
    }
    TTup _generators;
};

    template <class Trie>
    struct RegTrieAdapter : public OP::ranges::OrderedRangeOptimizedJoin< typename Trie::key_t, typename Trie::value_t>
    {
        using trie_t = Trie;
        using base_t = OP::ranges::OrderedRangeOptimizedJoin< typename trie_t::key_t, typename trie_t::value_t>;
        using iterator = typename base_t::iterator;
        using key_t = typename base_t::key_t;
        using value_t = typename base_t::value_t;
        using origin_iterator_t = typename trie_t::iterator;
        using payload_t = Payload<trie_t>;
        std::unique_ptr<GenArrayBase<trie_t> > _generators;

        RegTrieAdapter(std::shared_ptr<const trie_t> parent) noexcept
            : base_t([](const typename trie_t::key_t& left, const typename trie_t::key_t& right) -> int { return left.compare(right);})
            , _parent(std::move(parent))
        {
            //all must start with 'k'
            auto stp1 = std::make_tuple(std::move([](const trie_t& trie, payload_t& context, bool first)->bool {
                if (first)
                {
                    auto out = trie.find("k"_astr);
                    if( trie.in_range(out) )
                    {
                        context.push( out );
                        return true;
                    }
                    return false;
                }
                //only one is allowed
                context.pop();
                return false; 
                }));
            //all iterates must go over omega
            auto stp2 = std::tuple_cat(std::move(stp1), 
                std::make_tuple( std::move(
            [](const trie_t& trie, payload_t& context, bool first)->bool {
                if (first)
                {
                    auto out = trie.first_child(context.current_iterator());
                    if( trie.in_range(out)) 
                    {
                        context.push(out);
                        return true;
                    }
                    return false;
                }
                trie.next(context.current_iterator());
                if( !trie.in_range(context.current_iterator()) || !context.matches() )
                {
                    context.pop();
                    return false;
                }
                return true;
            })));
            auto stp3 = std::tuple_cat(std::move(stp2),
                std::make_tuple(std::move(
            [](const trie_t& trie, payload_t& context, bool first) ->bool {
                        //after omega must go separator
                        if (first)
                {
                    auto out = trie.find(context.current_iterator(), "k"_astr);
                    if( trie.in_range(out))
                    {
                        context.push(out);
                        return true;
                    }
                    return false;
                }
                context.pop();
                return false; //stop, no next
            })));
            auto stp4 = std::tuple_cat(std::move(stp3),
                std::make_tuple(std::move(
            //after separator must go meta
            [](const trie_t& trie, payload_t& context, bool first)->bool {
                if (first)
                {
                    auto out = trie.first_child(context.current_iterator());
                    if (trie.in_range(out))
                    {
                        context.push(out);
                        return true;
                    }
                    return false;
                }
                trie.next(context.current_iterator());
                if (!trie.in_range(context.current_iterator()) || !context.matches() )
                {
                    context.pop();
                    return false;
                }
                return true;
                })));
            _generators.reset( new GenArray<trie_t, decltype(stp4)>(std::move(stp4)) );
        }

        iterator begin() const override
        {
            std::unique_ptr<payload_t> pld(new payload_t{});
            bool is_first = true;
            for(size_t g = 0; g < _generators->size();)
            {
                if( !_generators->call(g, *_parent, *pld, is_first) )
                { //need step back
                    if(g == 0)
                    {
                        assert(pld->context_empty());
                        break;
                    }
                    is_first = false;
                    --g;
                    continue;
                }
                is_first = true;
                ++g;
            }
            return iterator(
                std::const_pointer_cast<range_t const>(shared_from_this()),
                std::move(pld)
                );
        }

        bool in_range(const iterator& check) const override
        {
            if (!check)
                return false;
            const payload_t& pld = check.impl< payload_t >();
            return !pld.context_empty();
        }
        void next(iterator& pos) const override
        {
            payload_t& pld = pos.impl< payload_t >();
            bool is_first = false;
            for ( size_t g = _generators->size()-1; !pld.context_empty() && g < _generators->size(); )
            {
                if (!_generators->call( g, *_parent, pld, is_first))
                {  //need step back
                    if (g == 0)
                    {
                        assert(pld.context_empty());
                        break;
                    }
                    is_first = false;
                    --g;
                    continue;
                }
                is_first = true;
                ++g;
            }
        }

        iterator lower_bound(const key_t& key) const override
        {
            throw std::exception("not implemented");
        }

        void next_lower_bound_of(iterator& i, const key_t& k) const override
        {
            // next_lower_bound_of(i.impl< payload_t >()._mixer_it, k);
            throw std::exception("not implemented");
        }

    private:
        
        std::shared_ptr<const trie_t> _parent;
    };
