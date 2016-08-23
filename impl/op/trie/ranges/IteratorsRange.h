#ifndef _OP_TRIE_RANGES_ITERATORS_RANGE__H_
#define _OP_TRIE_RANGES_ITERATORS_RANGE__H_

#include <op/trie/ranges/SuffixRange.h>

namespace OP
{
    namespace trie
    {
        namespace details {

            template<class T,
                class D = void>
                struct DiscoverIteratorKey
            {   // default definition
                typedef D key_t;
            };

            template<class T>
            struct DiscoverIteratorKey<T, std::void_t< decltype(std::declval<typename T::reference>().first)> >
            {   // defined if iterator declared as std::map????::iterator and key-type detected as decltype of reference::first
                typedef typename std::remove_reference< decltype(std::declval<typename T::reference>().first) >::type key_t;
                const key_t& key(const T& i) const
                {
                    return i->first;
                }
            };

            template<class T>
            struct DiscoverIteratorKey<T, std::void_t<typename T::key_type> >
            {   // defined if iterator contains explicit definition of key_type
                typedef typename T::key_type key_t;
                const key_t& key(const T& i) const
                {
                    return i.key();
                }
            };

        } //ns:details

        template <class BaseIterator, class KeyDiscover>
        struct IteratorWrap : public BaseIterator
        {
            typedef typename KeyDiscover::key_t key_type;
            IteratorWrap(const BaseIterator & base_iter, const KeyDiscover &key_discover)
                : BaseIterator((base_iter))
                , _key_discover(key_discover)
            {}
            const typename KeyDiscover::key_t& key() const
            {
                return _key_discover.key(*this);
            }
        private:
            const KeyDiscover &_key_discover;
        };
        /**
        *
        */
        template <class Iterator, class KeyDiscover = details::DiscoverIteratorKey<Iterator> >
        struct IteratorsRange : public SuffixRange< IteratorWrap< Iterator, KeyDiscover> >
        {
            typedef typename KeyDiscover::key_t key_type;
            typedef Iterator origin_iter_t;
            IteratorsRange(const origin_iter_t& begin, const origin_iter_t& end, KeyDiscover key_discover = KeyDiscover() )
                : _begin(begin) 
                , _end(end)
                , _key_discover(key_discover)
            {
            }
            /**Extract iterators from arbitrary containers that supports operations: std::begin, std::end*/
            template <class Container>
            IteratorsRange(const Container& source, KeyDiscover key_discover = KeyDiscover())
                : IteratorsRange(std::begin(source), std::end(source), key_discover)
            {
            }
            iterator begin() const override
            {
                return iterator(_begin, _key_discover);
            }
            iterator end() const
            {
                return iterator(_end, _key_discover);
            }
            bool in_range(const iterator& check) const override
            {
                return check != _end;
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }
        private:
            origin_iter_t _begin, _end;
            KeyDiscover _key_discover;
        }; 
        template <class Container, class KeyDiscover = details::DiscoverIteratorKey<Container::const_iterator> >
        inline std::shared_ptr< IteratorsRange<typename Container::const_iterator, KeyDiscover> > make_iterators_range(const Container& co)
        {
            return std::make_shared<IteratorsRange<typename Container::const_iterator, KeyDiscover>>(co);
        }
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_ITERATORS_RANGE__H_
