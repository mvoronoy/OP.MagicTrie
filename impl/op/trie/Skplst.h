#ifndef _OP_TRIE_SKPLST__H_
#define _OP_TRIE_SKPLST__H_

#include <assert.h>
#include <array>
#include <numeric>
#include <memory>
#include <op/trie/Utils.h>

struct DummyEntryLock
{
};

template <class Traits>
struct ListEntry
{
    typedef Traits traits_t;
    typedef typename traits_t::ptr_t entry_ptr_t;
    typedef typename traits_t::key_t key_t;

    ListEntry():
        _count(0),
        _value(Traits::eos())
    {
    }
    
    /**@return always valid pointer, that can be used to insert item or find element. 
    *           To detect if value found just check if `(**result)` is not equal `ListEntity::eos` */
    entry_ptr_t* prev_lower_bound(key_t key, Traits& traits)
    {
        entry_ptr_t* ins_pos = &_value;
        for(; !Traits::is_eos(*ins_pos);
            ins_pos = &traits.next( *ins_pos ))
        {
            if( traits.less( key, traits.key( *ins_pos )))
                return ins_pos;
        }
        //run out of existing keys or entry is empty
        return ins_pos;
    }
    template <class F>
    entry_ptr_t pull_that( Traits& traits, F& f)
    {
        entry_ptr_t* ins_pos = &_value;
        entry_ptr_t* result = ins_pos;
        for(; !Traits::is_eos(*ins_pos);
            ins_pos = &traits.next( *ins_pos ))
        {
            if (f(*ins_pos))
            {
                auto rv = *ins_pos;
                if (ins_pos == result) //peek first item
                    _value = traits.next(*ins_pos);
                else
                    traits.set_next(*result, traits.next(*ins_pos));
                --_count;
                return rv;
            }
            result = ins_pos;
        }
        //run out of existing keys or entry is empty
        return *ins_pos;
    }
    std::atomic_size_t _count;
    entry_ptr_t _value;
};
template <class T, class Traits, size_t bitmask_size_c = 32>
struct Log2SkipList
{
    typedef T value_t;
    typedef Traits traits_t;
    
    typedef Log2SkipList<T, Traits, bitmask_size_c> this_t;

    Log2SkipList()
    {
    }
    
    T pull_not_less(traits_t& traits, typename Traits::key_t key)
    {
        auto index = key_slot(traits, key);
        for (;index < _entries.size(); ++index)
        {
            auto& ent = _entries[index];
            if (ent._count)
            {
                wr_lock lock(traits, index);
                if (ent._count) //apply double-check pattern
                {
                    auto pulled = ent.pull_that(traits, [key, traits, this](const T& t){
                        return !traits.less(traits.key(t), key);
                    });
                    if (!traits.is_eos(pulled))
                        return pulled;
                }
            }
        }
        return traits_t::eos();
    }
    void insert(traits_t& traits, T t)
    {
        auto key = traits.key(t);
        //using ln(n) algorithm to detect entry position
        auto index = key_slot(traits, key);
        auto& ent = _entries[index];
        wr_lock lock(traits, index);
        auto * ins_ptr = ent.prev_lower_bound(key, traits);
        traits.set_next(t, * ins_ptr);
        * ins_ptr = traits.deref( t );
        ent._count++;
    }
    
private:

    typedef ListEntry<Traits> list_entry_t;
    typedef std::array<list_entry_t, bitmask_size_c> entries_t;
    typedef typename entries_t::iterator inner_iterator_t;
    typedef typename entries_t::const_iterator const_inner_iterator_t;
    
    entries_t _entries;
    /**Using O(ln(n)) complexity evaluate slot for specific key*/
    template <class K>
    size_t key_slot(traits_t& traits, K& k)
    {
        auto count = bitmask_size_c;
        size_t it, step, first = 0;
        while (count > 0) 
        {
            it = first; 
            step = count / 2; 
            it+= step;
            if (traits.less(1 << it, k)) {
                first = ++it; 
                count -= step + 1; 
            }
            else
                count = step;
        }
        //after all `first` is not less entry for `key` or smthng > bitmask_size_c
        if( first >= bitmask_size_c )
        {//place the rest (that are too big) to last entry
            first = bitmask_size_c - 1;
        }
        return first;
    }
public:
    template <class Traits, 
            void (Traits::*start_op)(size_t slot_index), 
            void (Traits::*end_op)(size_t slot_index) >
    struct Guard
    {
        Guard(Traits& traits, size_t slot_index):
            _traits(traits),
            _slot_index(slot_index),
            _is_closed(false)
        {
            (_traits.*start_op)(_slot_index);
        }
        void close()
        {
            _is_closed = true;
        }
        ~Guard()
        {
            if (!_is_closed)
                (_traits.*end_op)(_slot_index);
        }
    private:
        Traits& _traits;
        size_t _slot_index;
        bool _is_closed;
    };
    typedef Guard<Traits, &Traits::write_lock, &Traits::write_unlock> wr_lock;
    template <class Container, class InnerIterator>
    struct SkipListIterator
    {
        friend Container;
        typedef SkipListIterator<Container, InnerIterator> this_t;
        typedef typename Container::value_t value_t;
        typedef typename Container::traits_t traits_t;
        this_t& operator ++ ()
        {   
            assert(_from != _to);
        
            if (!_traits->is_eos(_list_ptr))
            {
                _list_ptr = _traits->next(_list_ptr);
                if (!_traits->is_eos(_list_ptr))
                    return *this;
            }
            ++_from;
            seek();
            return *this;
        }
        bool operator == (const this_t& other) const
        {
            return (_list_ptr == other._list_ptr);
        }
        bool operator != (const this_t& other) const
        {
            return (_list_ptr != other._list_ptr);
        }
        value_t& operator *()
        {
            assert(_list_ptr);
            return _list_ptr;
        }
        const value_t& operator *() const
        {
            assert(_list_ptr);
            return *_list_ptr;
        }
    private:

        typedef typename Container::traits_t traits_t;
        typedef typename InnerIterator inner_iterator_t;
        typedef typename traits_t::ptr_t entry_ptr_t;

        SkipListIterator():
            _traits(nullptr),
            _list_ptr(Traits::eos())
        {
        }
        SkipListIterator(inner_iterator_t from, inner_iterator_t to, traits_t* traits):
            _from(from), 
            _to(to),
            _traits(traits),
            _list_ptr(nullptr)
        {
            seek();
        }
        bool seek()
        {
            for (; _from != _to; ++_from)
            {
                _list_ptr = _from->_value;
                if (!_traits->is_eos(_list_ptr))//this slot is not empty
                    return true;
            }
            return false;
        }
        inner_iterator_t _from, _to;
        entry_ptr_t _list_ptr;
    };
    typedef SkipListIterator<this_t, inner_iterator_t> iterator;
    typedef SkipListIterator<this_t, const_inner_iterator_t> const_iterator;
    private:
    const_iterator begin() const
    {
        return const_iterator(_entries.begin(), _entries.end(), &_traits);
    }
    const_iterator end() const
    {
        return iterator();
    }
    iterator begin() 
    {
        return iterator(_entries.begin(), _entries.end(), &_traits);
    }
    iterator end() 
    {
        return iterator();
    }
};

#endif //_OP_TRIE_SKPLST__H_
