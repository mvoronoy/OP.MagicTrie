#pragma once
#ifndef _OP_TRIE_SPANCONTAINER__H_
#define _OP_TRIE_SPANCONTAINER__H_

#include <type_traits>
#include <list>
#include <stack>
#include <algorithm>
#include <memory>
#include <array>

#include <op/common/Range.h>
#include <op/common/PreallocCache.h>

namespace OP::zones
{
        
        /** Quick access data structure that allow efficiency ( O(Ln) on intersection algorithm) store of Span/Range data structure 
        with adding quick access by spatial indexing.
        Data-structure optimized to find intersection between items.
        Container may be used for conception of 'set' and 'map' (see SpanIdentityTraits and SpanMapTraits correspondingly)
        */
    template <class Span, class SpanTraits>
    class SpanContainer
    {
    public:
        using this_t = SpanContainer<Span, SpanTraits>;
        using traits_t = SpanTraits;

        using key_t = typename traits_t::key_t;
        using value_t = typename traits_t::value_t;

        using Storage = std::list<value_t>;
        using storage_iter_t = typename Storage::iterator;
        using next_f = std::function<void(storage_iter_t&)>;

        /** Implement iterator type */
        class Nav
        {
            friend this_t;
            storage_iter_t _data_pos;
            next_f _next;

        public:
            Nav() = default;
            template <class F>
            Nav(storage_iter_t data_pos, F do_next)
                :_data_pos(data_pos)
                , _next(std::move(do_next)) {}

            const key_t& span() const
            {
                return traits_t::keyof(*_data_pos);
            }
            operator storage_iter_t& ()
            {
                return _data_pos;
            }
            Nav& operator ++()
            {
                _next(_data_pos);
                return *this;
            }
            Nav operator ++(int)
            {
                Nav res(*this);
                _next(_data_pos);
                return *res;
            }
            value_t& operator*()
            {
                return *_data_pos;
            }
            const value_t& operator *()const
            {
                return *_data_pos;
            }
            value_t* operator->()
            {
                return _data_pos.operator->();
            }
            const value_t* operator ->()const
            {
                return _data_pos.operator->();
            }
            bool operator == (const Nav& other)const
            {
                return _data_pos == other._data_pos;
            }
            bool operator != (const Nav& other)const
            {
                return _data_pos != other._data_pos;
            }
            template <class F>
            void update_pos(F update, Nav* after)
            {
                *after = *this;
            }
        };
        using iterator = Nav;

        struct Node
        {
            virtual ~Node() = default;
            virtual bool is_terminal() const noexcept = 0;
            virtual const key_t& span() const = 0;
        };

        using slot_t = std::unique_ptr<Node, OP::common::details::CacheRecycleDeleter>;

        struct Terminal : public Node
        {
            explicit Terminal(const storage_iter_t& position) noexcept
                : _first(position)
                , _second(position)
            {
            }

            bool is_terminal() const noexcept override
            {
                return true;
            }

            const key_t& span() const override
            {
                return traits_t::keyof(*_first);
            }

            storage_iter_t _first, _second;
        };

        struct Index : public Node
        {
            enum
            {
                Cardinality = 3
            };

            Index() noexcept
                : Index(key_t{})
            {}

            explicit Index(key_t span) noexcept
                : _span(std::move(span))
            {}

            bool is_terminal() const noexcept override
            {
                return false;
            }

            const key_t& span() const override
            {
                return _span;
            }
                
            bool in_range(std::uint8_t idx) const
            {
                return idx < _interrior.size() //aka < Cardinality
                    && _interrior[idx] //non-null, since values go without gap
                    ;
            }

            key_t _span;
            std::array< slot_t, Cardinality > _interrior;
        };
        using index_slot_t = std::unique_ptr<Index, OP::common::details::CacheRecycleDeleter>;

        using index_cache_t = OP::common::PreallocCache<Index, traits_t::pre_allocate_nav_c>;
        using terminal_cache_t = OP::common::PreallocCache<Terminal, traits_t::pre_allocate_terminals_c>;

        class IntersectionTracker
        {
            friend this_t;
            using state_t = std::stack< std::pair<Index*, std::uint_fast8_t> >;

            state_t _breadcrumbs;
            const storage_iter_t _end;
            const key_t _boundary;
            struct LeafState
            {
                Terminal* _leaf;
                storage_iter_t _iter;
            };
            mutable LeafState _current{};
        public:

            IntersectionTracker(SpanContainer &from, key_t boundary) noexcept
                : _end(from._store.end())
                , _boundary(std::move(boundary))
            {                        
                if( boundary.count() && _boundary_check(from._index) && from._index->in_range(0) )
                {
                    _breadcrumbs.emplace(from._index.get(), 0);
                    _fetch_on_background();
                }
            }

            struct SpLock
            {
                std::atomic_flag& _flag;
                explicit SpLock(std::atomic_flag& flag)
                    :_flag(flag)
                {
                    while (_flag.test_and_set(std::memory_order_acquire))  // acquire lock
                        ; // spin
                }

                ~SpLock()
                {
                    _flag.clear(std::memory_order_release);               // release lock
                }
            };

            IntersectionTracker() noexcept = default;
            IntersectionTracker(IntersectionTracker&& ) noexcept = default;
            IntersectionTracker(const IntersectionTracker& other) = default;

            ~IntersectionTracker() = default;

            storage_iter_t end() const noexcept
            {
                return _end;
            }

            storage_iter_t current() const noexcept
            {
                return _current._leaf ? _current._iter : _end;
            }

            auto operator -> ()
            {
                if(!_current._leaf)
                    throw std::out_of_range("run out of bound");
                return _current._iter.operator ->();
            }

            auto operator * ()
            {
                if(!_current._leaf)
                    throw std::out_of_range("run out of bound");
                return _current._iter.operator *();
            }

            bool valid() const
            {
                return _current._leaf && _current._iter != _end ;
            }

            operator bool() const
            {
                return valid();
            }

            bool operator !() const
            {
                return !valid();
            }
                
            bool operator == (const IntersectionTracker& other) const
            {
                return _current._leaf == other._current._leaf 
                    && _current._iter == other._current._iter;
            }
                
            IntersectionTracker& operator++()
            {
                if(!_current._leaf)
                    throw std::out_of_range("run out of bound");
                else
                {
                    auto iter = _current._iter++;
                    if( _current._iter == _end || _current._leaf->_second == iter)
                    {
                        _fetch_on_background();
                    }
                } 
                return *this;
            }

            void operator()(storage_iter_t& iter)
            {
                operator++();
                iter = _current._iter;
            }

        private:

            void _fetch_on_background()
            {
                _current = _next();
            }
                
            /**Implement no-recursion deep-first algorithm picking left-most element on each branching*/
            LeafState _next()
            {
                while (!_breadcrumbs.empty())
                {
                    for(auto* pos = &_breadcrumbs.top(); pos->first->in_range(pos->second); )
                    {
                        Node* child = pos->first->_interrior[pos->second].get();
                        ++pos->second;
                        if( _boundary_check(child) )
                        {
                            if(child->is_terminal())
                            {
                                auto* term = static_cast<Terminal*>(child);
                                return LeafState{term, term->_first};
                            }
                            _breadcrumbs.emplace(
                                static_cast<Index*>(child), 
                                0
                            );
                            pos = &_breadcrumbs.top();
                        }
                    }
                    _breadcrumbs.pop();
                }
                return LeafState {nullptr, _end};
            }

            template <class NodePtr>
            bool _boundary_check(const NodePtr& from) const
            {
                return OP::zones::is_overlapping(_boundary, from->span());
            }
                
        };//IntersectionTracker

        SpanContainer()
            :_index{ _index_cache.allocate() }
        {
        }

        ~SpanContainer()
        {
            _clear(false);
        }

        /** add new entry */
        template <class... Args>
        iterator emplace(Args&&... args)
        {
            _store.emplace_front(std::forward<Args>(args)...);
            auto ins_pos = _store.begin();
            Nav res(ins_pos, default_next);
            return add_index(res);
        }

        /** Start iteration over all elements */
        iterator begin()
        {
            return Nav{ _store.begin(), default_next };
        }

        /** end of iteration */
        iterator end()
        {
            return Nav(_store.end(), default_next);
        }

        /** Start iteration over elements that intersects specified boundary */
        iterator intersect_with(key_t with) 
        {
            IntersectionTracker track(*this, std::move(with));
            return !track? end() : Nav(track.current(), track);
        }

        /** Only for debug purpose! It uses recursion for proofing purpose and should be used against large set */
        template <class Os>
        Os& dump(Os& os)
        {
            dump(os, 0, _index.get());
            return os;
        }
        /** Erase item specified by iterator from container.
        @return number of erased (0 or 1) 
            
        */
        size_t erase(const iterator& pos)
        {
            if(pos._data_pos == _store.end() 
                || !OP::zones::is_overlapping(traits_t::keyof(*pos), _index->span()))
                return 0; //nothing to do
                
            for(IntersectionTracker track(*this, traits_t::keyof(*pos._data_pos));
                track;
                ++track
                )
            {
                auto * term = track._current._leaf;
                bool is_one_item = term->_first == term->_second;
                bool fix_last = term->_second == pos._data_pos;
                bool fix_first = term->_first == pos._data_pos;
                for(auto i = term->_first;; ++i)
                {
                    if( i == pos._data_pos )
                    {
                        auto nxt_itm = _store.erase(i);
                        if( is_one_item )
                        {
                            _erase_top_item(track._breadcrumbs);
                            _optimize_up(track._breadcrumbs);
                        }else if(fix_last){
                            term->_second = --nxt_itm;
                        }else if(fix_first){
                            term->_first = nxt_itm;
                        }
                        return 1;
                    }
                    if (i == term->_second)
                        break;
                }
            }
            return 0;
        }
        void clear()
        {
            _clear(true);
        }

    private:
        static void default_next(storage_iter_t& iter)
        {
            ++iter;
        }

        void _clear(bool restart)
        {
            std::vector<index_slot_t> postponed;
            postponed.emplace_back(std::move(_index));//add root
            while(!postponed.empty())
            {
                auto current = std::move(postponed.back());
                postponed.pop_back();
                for(auto& node : current->_interrior)
                {
                    if(!node)
                        break;
                    else if(!node->is_terminal())
                    {
                        postponed.emplace_back(static_cast<Index*>(node.release()));
                    }
                    else
                        node.reset();
                }
            }
            _store.clear();
            if(restart)
                _index = _index_cache.allocate();
        }

        /**Add one more data iterator position to 'target' index */
        void _append_data_ref(Terminal& target, Nav& to_update)
        {
            //move internal list's element closer to Index
            std::advance(target._second, 1);
            _store.splice(target._second, _store, to_update._data_pos);
            target._second = to_update._data_pos;
        }

        /**Using std::move shifts items in array*/
        template <class Iter>
        static void _move_items(Iter from, Iter to, Iter dest)
        {
            for(; from != to; ++from, ++dest)
                *dest = std::move(*from);
        }
            
        template <class Breadcrumbs>
        static void _erase_top_item(Breadcrumbs& breadcrumbs)
        {
            auto* current = &breadcrumbs.top();
            // (-1) used since tracker already advanced index by +1
            current->first->_interrior[current->second-1].reset();
            _move_items(
                current->first->_interrior.begin() + current->second, 
                current->first->_interrior.end(),
                current->first->_interrior.begin() + current->second - 1
                );
        }
        /**When node deleted need to check if parent indexed node became empty*/
        template <class Breadcrumbs>
        static void _optimize_up(Breadcrumbs& breadcrumbs)
        {
            for(auto* current = &breadcrumbs.top(); 
                !current->first->in_range(0) //check against 0 only because gaps are not allowed
                && breadcrumbs.size() > 1; current = &breadcrumbs.top()) // don't remove root index
            {
                breadcrumbs.pop();
                _erase_top_item(breadcrumbs);
            }
            //now need to re-calculate bounds for the rest of parents
            for(auto* current = &breadcrumbs.top(); ; ) 
            {
                current->first->_span = typename traits_t::key_t{};
                for(auto& uptr: current->first->_interrior)
                {
                    if(!uptr)
                        break;//null means last item, since no gaps are allowed
                    current->first->_span = OP::zones::unite_zones(current->first->_span, uptr->span());
                }
                breadcrumbs.pop();
                if(breadcrumbs.empty())
                    break;
                current = &breadcrumbs.top();
            }
        }

        template <class Os>
        void dump(Os& os, size_t ident, Node* current)
        {
            os << std::setw(ident * 4) << std::setfill(' ') << "";
            if (!current)
                os << "null\n";
            else if (current->is_terminal())
            {
                os << "t{";
                size_t cnt = 0;
                auto* t = static_cast<Terminal*>(current);
                for (auto i = t->_first; true; ++i, ++cnt)
                {
                    os << (cnt == 0 ? "" : ", ") << *i;
                    if (i == t->_second)
                        break;
                }
                os << "}\n";
            }
            else {
                auto* index_node = static_cast<Index*>(current);
                os << "i{" << index_node->span() << ":\n";
                for (auto& i : index_node->_interrior)
                {
                    dump(os, ident + 1, i.get());
                }
                os << std::setw(ident * 4) << std::setfill(' ') << ""/*need to fill aligned*/ << "}\n";
            }

        }

        Nav& add_index(Nav& position)
        {
            Index* current = _index.get();
            current->_span = traits_t::unite(current->_span, position.span()); //always extend index with new span
            int i = 0; //must be signed-value!!!
            for (; i < Index::Cardinality; ++i)
            {
                auto& node = current->_interrior[i];
                if (!node)//empty slot => just occupy
                {
                    node = _terminal_cache.allocate(position);
                    return position;
                }
                if (OP::zones::is_overlapping(node->span(), position.span()))
                {
                    if (!node->is_terminal())
                    {//non-terminal, dive inside index
                        current = static_cast<Index*>(node.get());
                        current->_span = OP::zones::unite_zones(current->_span, position.span());
                        i = -1; //on next step became == 0
                        continue;
                    }
                    if (node->span() == position.span())
                    {//exact equals
                        _append_data_ref(*static_cast<Terminal*>(node.get()), position/*!modified inside*/);
                        return position;
                    }
                    if (current->_interrior.back()) //cannot shift right since last slot is already occupied
                    {
                        auto new_index = _index_cache.allocate(
                            OP::zones::unite_zones(node->span(), position.span()));
                        std::uint_fast8_t i_exist = 1, i_new = 0;
                        if( node->span().pos() < position.span().pos() )
                            std::swap(i_exist, i_new);

                        new_index->_interrior[i_new] = _terminal_cache.allocate(position);
                        new_index->_interrior[i_exist] = std::move(current->_interrior[i]);
                        current->_interrior[i] = std::move(new_index);
                    }
                    else //may shift all elements right
                    {
                        std::rotate(current->_interrior.rbegin(), current->_interrior.rbegin() + 1, current->_interrior.rend() - i);
                        current->_interrior[i] = _terminal_cache.allocate(position);
                    }
                } else {
                    if (node->span().pos() < position.span().pos())
                    {
                        continue; //pass to ++i
                    }
                    //here since not-equal but bigger
                    //here since position is candidate to paste
                    if (current->_interrior.back()) //cannot shift right since last slot is already occupied
                    {  //let's create new Index without over-occupied slots

                        auto new_index = _index_cache.allocate(
                            OP::zones::unite_zones(current->_interrior[i]->span(), position.span()));
                        new_index->_interrior[0] = _terminal_cache.allocate(position);
                        new_index->_interrior[1] = std::move(current->_interrior[i]);
                        current->_interrior[i] = std::move(new_index);
                    }
                    else //may shift all elements right
                    {
                        std::rotate(current->_interrior.rbegin(), current->_interrior.rbegin() + 1, current->_interrior.rend() - i);
                        current->_interrior[i] = _terminal_cache.allocate(position);
                    }
                }
                return position;
            }//for: over interrior
            //`position` is still bigger than max in current, create rightmost non-terminal leaf
            auto new_index = _index_cache.allocate(
                OP::zones::unite_zones(current->_interrior.back()->span(), position.span()));
            new_index->_interrior[0] = std::move(current->_interrior.back());
            new_index->_interrior[1] = _terminal_cache.allocate(position);
            current->_interrior.back() = std::move(new_index);

            return position;
        }

        index_cache_t _index_cache;
        terminal_cache_t _terminal_cache;

        index_slot_t _index;
        Storage _store;
    }; //SpanContainer
        
    /** Utility that should be used as 2-nd template argument of SpanContainer when it is
        going to be used as a 'set' instead of 'map'
    */     
    template <class Span>
    struct SpanIdentityTraits
    {
        using key_t = Span;
        using value_t = Span;

        constexpr static size_t pre_allocate_terminals_c = 256;
        constexpr static size_t pre_allocate_nav_c = 256;

        static const key_t& keyof(const value_t& v)
        {
            return v;
        }
            
        static key_t unite(const key_t& a, const key_t& b)
        {
            return OP::zones::unite_zones(a, b);
        }
    };


    /** Utility that should be used as 2-nd template argument of SpanContainer when it is
        going to be used as a 'map' to store pair values
    */
    template <class Span, class Value>
    struct SpanMapTraits
    {
        using pair_t = std::pair< Span, Value >;
        using key_t = Span;
        using value_t = pair_t;

        constexpr static size_t pre_allocate_terminals_c = 256;
        constexpr static size_t pre_allocate_nav_c = 256;

        static const key_t& keyof(const value_t& v)
        {
            return v.first;
        }
        static key_t unite(const key_t&a, const key_t&b)
        {
            return OP::zones::unite_zones(a, b);
        }
    };
        
    template <class Span, class Value>
    using SpanMap = SpanContainer< std::pair<Span, Value>, SpanMapTraits<Span, Value> >;

    template <class Span>
    using SpanSet = SpanContainer< Span, SpanIdentityTraits<Span> >;

}//ns:OP::zones

#endif //_OP_TRIE_SPANCONTAINER__H_
