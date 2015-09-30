#include "stdafx.h"
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <assert.h>
#include <array>
#include <numeric>
#include <memory>
#include <set>

struct Nav
{
    int key;
    int entries;
    typedef std::shared_ptr<Nav> nav_ptr;
    nav_ptr _next;
    Nav():key(~0u), entries(0){}
    Nav(size_t key):key(key), entries(1){}
    bool operator < (const Nav& right) const
    {
        return this->key < right.key;
    }
};
typedef std::shared_ptr<Nav> nav_ptr;

struct DummyEntryLock
{
};
template <class Traits>
struct ListEntry
{
    typedef Traits traits_t;
    typedef typename traits_t::ptr_t entry_ptr_t;
    typedef typename traits_t::key_t key_t;
    typedef typename traits_t::writelock_t writelock_t;

    ListEntry():
        _count(0),
        _value(Traits::eos())
    {
    }
    
    /**@return always valid pointer, that can be used to insert item or find element. 
    *           To detect if value found just check if `(**result)` is not equal `ListEntity::eos` */
    entry_ptr_t* prev_lower_bound(key_t key, const Traits& traits)
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
    entry_ptr_t pull_that(const Traits& traits, F& f)
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
                    traits.next(*result) = traits.next(*ins_pos);
                --_count;
                return rv;
            }
            result = ins_pos;
        }
        //run out of existing keys or entry is empty
        return *ins_pos;
    }
    size_t _count;
    entry_ptr_t _value;
};
template <class T, class Traits, size_t bitmask_size_c = 32>
struct Log2SkipList
{
    typedef T value_t;
    typedef Traits traits_t;
    
    typedef Log2SkipList<T, Traits, bitmask_size_c> this_t;

    Log2SkipList(Traits&& traits = Traits()):
        _traits(std::move(traits))
    {
    }
    T lower_bound(T t)
    {
        auto key = _traits.key(t);
        auto index = key_slot(key);
        auto& ent = _entries[index];
        wr_lock lock(_traits, index );
        auto * ins_ptr = ent.prev_lower_bound(key, _traits);
        return _traits.next( * ins_ptr )
    }
    T pull_not_less(T t)
    {
        auto key = _traits.key(t);
        auto index = key_slot(key);
        auto& ent = _entries[index];
        wr_lock lock(_traits, index );
        return ent.pull_that(_traits, [key, this](const T& t){
            return !_traits.less(_traits.key(t), key);
        });
    }
    void insert(T t)
    {
        auto key = _traits.key(t);
        //using ln(n) algorithm to detect entry position
        auto index = key_slot(key);
        auto& ent = _entries[index];
        wr_lock lock(_traits, index);
        auto * ins_ptr = ent.prev_lower_bound(key, _traits);
        _traits.next( t ) = * ins_ptr;
        * ins_ptr = _traits.deref( t );
        ent._count++;
    }
    
private:

    typedef ListEntry<Traits> list_entry_t;
    typedef std::array<list_entry_t, bitmask_size_c> entries_t;
    typedef typename entries_t::iterator inner_iterator_t;
    typedef typename entries_t::const_iterator const_inner_iterator_t;
    
    entries_t _entries;
    Traits _traits;
    /**Using O(ln(n)) complexity evaluate slot for specific key*/
    template <class K>
    size_t key_slot(K& k)
    {
        auto count = bitmask_size_c;
        size_t it, step, first = 0;
        while (count > 0) 
        {
            it = first; 
            step = count / 2; 
            it+= step;
            if (_traits.less(1 << it, k)) {
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
        traits_t* _traits;
    };
    typedef SkipListIterator<this_t, inner_iterator_t> iterator;
    typedef SkipListIterator<this_t, const_inner_iterator_t> const_iterator;
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


template <class Os, class A>
Os& print(Os& os, const A& a)
{
    for(auto i = a.begin(); i != a.end(); ++i)
        os << (i != a.begin() ? ", " : "") << (*i);
    return os;    
}

struct TestTrait
{
    typedef size_t key_t;
    typedef nav_ptr ptr_t;
    typedef DummyEntryLock writelock_t;
    static inline const ptr_t eos()
    {
        return nullptr;
    }
    static bool is_eos(ptr_t pt)
    {
        return pt == nullptr;
    }
    static bool less(key_t left, key_t right)
    {
        return left < right;
    }
    void write_lock(size_t idx)
    {
    }
    void write_unlock(size_t idx)
    {
    }
    static key_t key(const nav_ptr& n)
    {
        
        const size_t range = (512 + 256) / 2;
        if (n->key > range)//key is in range[0..1000), so unfold to [0..2^32)
        {
            return ((n->key - range)* ((1ull << 32) - 1)) / (512. * std::log10(n->key));
        }
        return n->key;
    }
    
    static ptr_t& next(nav_ptr& n)
    {
        return n->_next;
    }
    static ptr_t next(const nav_ptr& n)
    {
        return n->_next;
    }
    template <class T>
    static T& deref(T& n)
    {
        return n;
    }

};
void random_population()
{
    std::vector<int> rnd;
    rnd.resize(1000);
    std::iota(rnd.begin(), rnd.end(), 0);//fill ascending
    std::random_shuffle(rnd.begin(), rnd.end());
    //print(std::cout, rnd)<<std::endl;

    std::cout << "=========\n";
    typedef Log2SkipList<nav_ptr, TestTrait> skplst_t;
    skplst_t lst;
    for (auto i : rnd)
    {
        lst.insert(std::make_shared<Nav>(i));
    }
    int prev = -1;
    for (auto r : lst)
    {
        auto n = (*r).key;
        assert(n > prev);
        prev = n;
    }
    //shuffle once again
    std::random_shuffle(rnd.begin(), rnd.end());
    for (auto idx : rnd)
    {
        auto x = lst.pull_not_less(std::make_shared<Nav>(idx));
        assert(x->key == idx);
    }
    std::cout << std::endl;
}

int main()
{
    typedef Log2SkipList<nav_ptr, TestTrait> skplst_t;
    skplst_t lst;
    lst.insert(std::make_shared<Nav>(266));
    lst.insert(std::make_shared<Nav>(263));
    lst.insert(std::make_shared<Nav>(260));

    lst.insert(std::make_shared<Nav>(516));
    lst.insert(std::make_shared<Nav>(513));
    lst.insert(std::make_shared<Nav>(514));
    
    lst.insert(std::make_shared<Nav>(5));
    lst.insert(std::make_shared<Nav>(4));
    lst.insert(std::make_shared<Nav>(8));
    lst.insert(std::make_shared<Nav>(9));
    lst.insert(std::make_shared<Nav>(1));
    lst.insert(std::make_shared<Nav>(7));
    lst.insert(std::make_shared<Nav>(0));
    lst.insert(std::make_shared<Nav>(2));
    lst.insert(std::make_shared<Nav>(3));
    lst.insert(std::make_shared<Nav>(6));

    for (auto r : lst)
    {
        std::cout << '{' << (*r).key << "},";
    }
    std::cout << "\npull:" << lst.pull_not_less(std::make_shared<Nav>(6))->key ;
    std::cout << "\npull:" << lst.pull_not_less(std::make_shared<Nav>(5))->key ;
    std::cout << "\npull:" << lst.pull_not_less(std::make_shared<Nav>(8))->key ;
    std::cout << "\npull:" << lst.pull_not_less(std::make_shared<Nav>(7))->key ;
    std::cout << std::endl;
    for (auto r : lst)
    {
        std::cout << '{' << (*r).key << "},";
    }
    std::cout << std::endl;
    random_population();
    return 0;
}
