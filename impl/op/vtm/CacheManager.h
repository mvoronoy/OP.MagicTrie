#ifndef _OP_TR_CACHEMANAGER__H_
#define _OP_TR_CACHEMANAGER__H_

#include <unordered_map>
#include <future>
#include <mutex> 
#include <shared_mutex>
#include <cassert>
#include <op/common/Utils.h>

namespace OP::trie
{
    template <class Key, class Value, class Hasher = std::hash<Key> >
    struct CacheManager
    {
        typedef std::function<void(void)> background_task_t;
        typedef std::function<void(background_task_t)> background_task_producer_t;
        static void default_background_producer (background_task_t t) 
        {
            t();
        };
        CacheManager(size_t limit, background_task_producer_t p= default_background_producer)
            : _limit(limit)
            , _background_producer(p)
            , _root(new ReferenceKey(root_reference_key_t()))
        {
        }
        /**
        * @throws std::out_of_range if k is not found
        */
        Value get(const Key& k)
        {
            auto rk = std::make_unique<ReferenceKey>(k);
            guard_t l(_map_lock);
            typename map_t::iterator found = _map.find(rk);
            if (found != _map.end())
            {
                found->first->float_up(_root.get());
                return found->second;
            }
            throw std::out_of_range("no such key");
        }

        template<class Factory>
        Value get(const Key& k, Factory& f)
        {
            auto rk = std::make_unique<ReferenceKey>(k);
            guard_t l(_map_lock);

            typename map_t::iterator found = _map.find(rk);
            if (found == _map.end())
            {
                //cycling list have biggest 'prev' root
                auto inserted = this->_map.insert(
                    std::make_pair(std::move(rk), f(k)));
                ReferenceKey::insert_after(this->_root->prev(), inserted.first->first.get());
                shrink();
            
                return inserted.first->second;
            }
        
            found->first->float_up(_root.get());
            return found->second;
        }
        void put(const Key& k, Value&& val, bool override_if_exists=true)
        {
            auto rk = std::make_unique<ReferenceKey>(k);
            guard_t l(_map_lock);
            typename map_t::iterator found = _map.find(rk);
            if (found == _map.end())
            {
                auto inserted = this->_map.insert(
                    std::make_pair(std::move(rk), std::move(val)));
                ReferenceKey::insert_after(this->_root->prev(), inserted.first->first.get());
                shrink();
            }
            else
            {
                if (override_if_exists)
                    found->second = std::move(val);
                //raise priority
                found->first->float_up(_root.get());
            }
        }
        template <class F>
        void set_erase_callback(F& f)
        {
            _erase_callback = f;
        }
        bool erase(const Key& k)
        {
            guard_t l(_map_lock);
            return _erase(k);
        }
        /** Forces to remove key from cache, method starts erase in background */
        std::future<bool> invalidate(const Key& k)
        {
            return std::async(
                std::launch::async, 
                [this](const Key& k)
                { 
                    return this->erase(k); 
                }, 
                k);
        }

        size_t limit() const
        {
            return _limit;
        }
        size_t size() const
        {
            guard_t l(_map_lock);
            return _map.size();
        }
        void _check_integrity()
        {
            guard_t l(_map_lock);
            if (_root->next() == _root.get()) //root references itself
            {
                assert(_root->next() == _root->prev());
                assert(_map.empty());
                return;
            }
            size_t count = 0;
            ReferenceKey *check_prev = _root.get();
            for (auto p = _root->next(); p != _root.get(); p = p->next(), ++count)
            {
                assert(p->prev() == check_prev);
                assert(check_prev->use_count() <= p->use_count());
                check_prev = p;
                //make fake unique_ptr
                std::unique_ptr<ReferenceKey> temp_k(p);
                assert(_map.find(temp_k) != _map.end());
                temp_k.release(); //don't allow unique_ptr to delete pointer
            }
            assert(count == _map.size());
        }
    private:
        bool _erase(const Key& k)
        {
            auto rk = std::make_unique<ReferenceKey>(k);
            typename map_t::iterator found = _map.find(rk);
            if (found == _map.end())
                return false;
            on_before_erase(found->first->key, found->second);
            found->first->erase(_root.get());
            _map.erase(found);
            return true;
        }
        void on_before_erase(const Key& k, const Value& v) const
        {
            if (_erase_callback)
                _erase_callback(k, v);
        }
        /**on background shrink size to #limit if needed. Strongly needed that _map_lock obtained */
        void shrink()
        {
            if (this->limit() < this->_map.size())
            {
                auto f = [this]() {
                    guard_t l(this->_map_lock);
                    if (this->limit() < this->_map.size())//erase less used
                        this->_erase(this->_root->next()->key);
                };
                _background_producer(f);
            }
        }
        struct root_reference_key_t{};
        struct RefHasher;
        /**support cycling list*/
        struct ReferenceKey
        {
            friend struct RefHasher;
            typedef ReferenceKey this_t;
            /*ReferenceKey():
                _next(nullptr),
                _prev(nullptr),
                _use_count(0),
                key()
            {}*/
            /**Dedicated constructor for root node*/
            ReferenceKey(root_reference_key_t):
                key(),
                _use_count(0),
                _next(this),
                _prev(this)
            {};
            ReferenceKey(const ReferenceKey&) = delete;

            ReferenceKey(Key k):
                key(k),
                _use_count(0),
                _next(nullptr),
                _prev(nullptr)
            {

            }
            template <class Zhis, class ... Types>
            static std::unique_ptr<this_t> make_after(Zhis* after, Types&& ... args)
            {
                auto rv = std::unique_ptr<this_t>(new this_t(std::forward<Types>(args)...));
                insert_after(after, rv);
                return rv;
            }
        
            static void insert_after(this_t* after, this_t* what)
            {
                what->_use_count = after->_use_count + 1;
            
                after->_next->_prev = what;
                what->_next = after->_next;
                after->_next = what;

                what->_prev = after;
            }
            bool operator <(const ReferenceKey& other)const
            {
                return this->key < other.key;
            }
            bool operator ==(const ReferenceKey& other)const
            {
                return this->key == other.key;
            }
            ReferenceKey* next() const
            {
                return _next;
            }
            ReferenceKey* prev() const
            {
                return _prev;
            }
            unsigned use_count() const
            {
                return _use_count;
            }
            /**
            * Increase usage and climb-up on a list 
            */
            bool float_up(ReferenceKey* root) 
            {
                assert(_next);//in cyclic list _next & _prev must not be null
                assert(_prev);
                assert(_use_count > 0);//can't move root
                ++_use_count;
                if (root != _next /*&& _next->_use_count < this->_use_count*/)
                {
                    /*!*/_next->_use_count = this->_use_count;
                    auto t_next = _next;
                    this->_next = t_next->_next;
                    this->_next->_prev = this;
                    t_next->_next = this;
                
                
                    t_next->_prev = this->_prev;
                    this->_prev->_next = t_next;
                    this->_prev = t_next;
                
                    return true;
                }
                return false;
            }
            /**
            * @return biggest _use_count number or ~0u if unmodified
            */
            void erase(ReferenceKey* root)
            {
                assert(this != root);//root must not be erased
                assert(_prev);//in cyclic list _next & _prev must not be null
                this->_prev->_next = this->_next;
                assert(_next);
                this->_next->_prev = this->_prev;
                if (this == root->_prev)//lowest value
                {
                    for (auto p = this->_next; p != root; p = p->_next)
                    {
                        p->_use_count = p->_use_count - this->_use_count + 1; //normalize count usage
                    }
                }
            }
            const Key key;
        private:
            ReferenceKey(ReferenceKey*after, Key k):
                key(k),
                _use_count(_use_count),
                _next(nullptr),
                _prev(nullptr)
            {

            }

            mutable unsigned _use_count;
            mutable this_t* _next;
            mutable this_t* _prev;
        };
        typedef std::unique_ptr<ReferenceKey> refkey_ptr_t;
        struct RefHasher
        {
            size_t operator()(const refkey_ptr_t& k) const
            {
                return _hasher(k->key);
            }
            bool operator()(const refkey_ptr_t& l, const refkey_ptr_t& r) const
            {
                return l->key == r->key;
            }
            Hasher _hasher;
        };
        typedef std::unordered_map<refkey_ptr_t, Value, RefHasher, RefHasher> map_t;
        typedef std::recursive_mutex lock_t;
        typedef std::unique_lock<lock_t> guard_t;
        typedef std::function<void(const Key&, const Value&)> erase_callback_t;

        unsigned _mass_use_count; 
        mutable lock_t _map_lock;
        map_t _map;
        refkey_ptr_t _root;
        size_t _limit;
        erase_callback_t _erase_callback;
        background_task_producer_t _background_producer;
    };

    /**Simple thread-safe read-optimized storage of elements indexed in range [0...).*/
    template <class T, class K = size_t>
    struct SparseCache
    {
        typedef std::shared_ptr<T> ptr_t;
        typedef K index_t;
        SparseCache(index_t capacity)
            : _capacity(capacity)
            , _data(new ptr_t[capacity])
        {
            assert(_capacity > 0);
        }
        void put(index_t pos, ptr_t&& value)
        {
            std::unique_lock guard(_lock);
            ensure(pos);
            _data[pos] = std::move(value);
        }
        ptr_t get(index_t pos) const
        {
            std::shared_lock guard(_lock);
            if (pos >= _capacity)
                throw std::out_of_range("no such key");
            return _data[pos];
        }
        template <class Factory>
        ptr_t get(index_t pos, Factory factory)
        {
            do{ //control block to demarcate scope of guard
                std::shared_lock guard(_lock);
                if (pos < _capacity)
                {
                    auto r = _data[pos];
                    if(r.get())
                        return r;
                }
            } while(false);
            //need doublecheck presence
            std::unique_lock guard(_lock);
            ensure(pos);
            auto r = _data[pos];
            if (!r)
                _data[pos] = r = factory(pos);
            return r;
        }
    private:
            
        using container_t = std::unique_ptr<ptr_t[]>;

        const static index_t capacity_step_c = 16;

        index_t _capacity;
        container_t _data;
        mutable std::shared_mutex _lock;

        void ensure(index_t pos)
        {
            if(pos < _capacity)
                return;
            //ceil for nearest bigger of 'pos'
            auto new_capacity = (1+ pos / capacity_step_c)*capacity_step_c;
            container_t new_data = container_t(new ptr_t[new_capacity]);
            for (size_t i = 0; i < _capacity; ++i)
            {
                new_data[i] = std::move(_data[i]);
            }
            _capacity = new_capacity;
            _data = std::move(new_data);
        }
    };
    
}//ns: OP::trie

#endif //_OP_TR_CACHEMANAGER__H_
