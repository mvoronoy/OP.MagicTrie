#ifndef _OP_CONTAINERS__H_
#define _OP_CONTAINERS__H_

#ifdef _MSC_VER
#pragma warning( disable : 4351) //stupidies warning from VS /new behavior: elements of array 'array' will be default initialized/
#endif //_MSC_VER

#include <cstdint>
#include <type_traits>
#include <algorithm>

namespace OP
{
    namespace trie
    {
        /**the smallest part of trie key*/
        typedef std::uint8_t atom_t;
        /**unsigned size of single page(container) */
        typedef std::uint16_t node_size_t;

        struct EmptyPayload
        {};

        namespace _internal
        {
            template <bool tst, node_size_t _then, node_size_t _or_else>
            struct switch_value{
            };
            template <node_size_t _then, node_size_t _or_else>
            struct switch_value<true, _then, _or_else>{
                enum
                {
                    value_c = _then
                };
            };
            template <node_size_t _then, node_size_t _or_else>
            struct switch_value<false, _then, _or_else>{
                enum
                {
                    value_c = _or_else
                };
            };
            template <node_size_t node_size>
            struct max_hash_neighbors
            {
                enum
                {
                    value_c = switch_value<node_size == 8, 2,
                    switch_value<node_size == 16 || node_size == 32, 3,
                    switch_value<node_size == 64 || node_size == 128 || node_size == 256, 4, 0>::value_c
                    >::value_c
                    >::value_c
                };
            };

            inline unsigned has_neighbors(node_size_t page_c)
            {
                switch (page_c)
                {
                case 8:
                    return max_hash_neighbors<8>::value_c;
                case 16:
                    return max_hash_neighbors<16>::value_c;
                case 32:
                    return max_hash_neighbors<32>::value_c;
                case 64:
                    return max_hash_neighbors<64>::value_c;
                case 128:
                    return max_hash_neighbors<128>::value_c;
                case 256:
                    return max_hash_neighbors<256>::value_c;
                default:
                    throw std::invalid_argument("Allowed hashtables only with page size 8,16,32,64,128,256");
                }
            }
        }

        /**
        *   base for persistence containers that can store some byte range
        */
        template <class Payload>
        struct NavigableByteRangeContainer
        {
            typedef Payload payload_t;
            virtual ~NavigableByteRangeContainer() = default;
            /**
            *   Method inserts some part of source sequence (specified by [begin, end) ). Caller can check how
            *   many bytes were inserted by checking in/out parameter `begin`. For example you have source sequence "abcdef",
            *   for some reason container can only accomodate "abc" then insert will be successfull for "abc" and `begin` will point to "def"
            *   @param begin - (in/out) for input it is begin of sequence to insert, on out the first non-inserted character
            *   @param end - end of source sequence
            *   @return :
            *   \li if item is unique and there is container space - index inside container where insertion were done.
            *   \li if item already exists - index inside container where the item is placed (to distinct from prev. check that `begin` wasn't modified)
            *   \li if container cannot grow and accomodate item result is ~0u (unsigned presentation of -1)
            */
            virtual unsigned accommodate(const atom_t*& begin, const atom_t* end) = 0;
            /**
            * Find entry with max approximation to specified prefix.
            * Assume container owns by {"abc", "ac"}, then:
            * \li find_refix('a') == "abc" (lowest is taken)
            * \li find_prefix("b") == end()
            * \li find_prefix("abcde") == "abc"
            *
            * At exite param `begin` points to first unmatched symbol
            */
            virtual unsigned match_sequence(const atom_t*& begin, const atom_t* end) const = 0;
            virtual payload_t& value(unsigned key) = 0;
            //virtual const payload_t& value(unsigned key) const = 0;
        };
        /** base for containers that implements byte-key to value contract */
        template <class Payload>
        struct ByteKeyContainer
        {
            typedef Payload payload_t;
            virtual ~ByteKeyContainer() = default;
            /** Render and alocate unique key that is not exists in this container yet
            *@throws std::out_of_range exception if container exhausted and no more items can be allocated
            */
            virtual atom_t allocate_key() = 0;
            /**
            *   Get value placeholder for the key
            *   *@throws std::out_of_range exception if key is not exists
            */
            virtual payload_t& value(atom_t key) = 0;
            /** Erase the entry associated with key
            *   *@throws std::out_of_range exception if key is not exists
            */
            virtual void remove(atom_t key) = 0;
        };
        template <class Container>
        struct NodeTableIterator
        {
            typedef Container container_t;
#ifdef __MINGW32__
            friend class Container;
#else
            friend container_t;
#endif // __MINGW32__

            typedef NodeTableIterator<Container> this_t;
            typedef std::random_access_iterator_tag iterator_category;
            typedef typename Container::key_t value_type;
            typedef int difference_type;
            typedef int distance_type;
            typedef typename atom_t* pointer;
            typedef typename atom_t& reference;

            NodeTableIterator(const container_t* container, size_t offset) :
                _container(container),
                _offset(offset)
            {}
            this_t& operator ++()
            {
                _container->next(*this);
                return *this;
            }
            this_t operator ++(int)
            {
                this_t rv(*this);
                _container->next(*this);
                return rv;
            }
            bool operator == (const this_t& other) const
            {
                if (this->_offset != other._offset)
                    return false;
                //handle special case for end()
                if (this->_offset == ~0u)
                    return true;
                return this->_container == other._container;
            }
            bool operator != (const this_t& other) const
            {
                return !operator==(other);
            }
            this_t operator + (int value_c) const
            {
                this_t rv(*this);
                return _container->next(rv, value_c);
            }
            this_t operator - (int value_c) const
            {
                this_t rv(*this);
                return _container->next(rv, -value_c);
            }

            bool operator < (const this_t & other) const
            {
                return this->_offset < other._offset;
            }
            value_type operator *() const
            {
                return this->_container->key(*this);
            }
            unsigned _debug() const
            {
                return static_cast<unsigned>(this->_offset);
            }

        private:
            const container_t* _container;
            size_t _offset;
        };


        enum NodePersense
        {
            fpresence_c = 0x1,
            faddress_c = 0x2,
            fvalue_c = 0x4,
        };

        template <class Payload, node_size_t capacity>
        struct NodeHashTable : public ByteKeyContainer<Payload>
        {
            enum
            {
                capacity_c = capacity,
                bitmask_c = capacity_c - 1 /*on condition capacity is power of 2*/,
                neighbor_width_c = _internal::max_hash_neighbors<capacity_c>::value_c
            };
            typedef NodeHashTable<Payload, capacity_c> this_t;
            typedef NodeTableIterator<typename this_t> iterator;
            typedef atom_t key_t;

            static size_t allocation_size(node_size_t capacity)
            {
                return sizeof(Content) * capacity_c;
            }
            static const std::vector<node_size_t >& known_capacity()
            {
                static const std::vector<node_size_t > rv = { 8, 16, 32, 64, 128 };
                return rv;
            }

            NodeHashTable() :
                _count(0),
                _memory_block{}
            {

                /*
                memset(allocation, 0, allocation_size(head._capacity));
                assert(head._capacity == 8
                || head._capacity == 16
                || head._capacity == 32
                || head._capacity == 64
                || head._capacity == 128)
                //"Allowed hashtables only with page size 8,16,32,64,128")
                ;
                */
                static_assert(_internal::max_hash_neighbors<capacity_c>::value_c != 0, "Allowed hashtables only with page size 8,16,32,64,128");
            }
            node_size_t size() const
            {
                return _count;
            }
            bool empty() const
            {
                return size() == 0;
            }
            node_size_t capacity() const
            {
                return capacity_c;
            }

            iterator insert(atom_t key)
            {
                unsigned hash = static_cast<unsigned>(key)& bitmask_c;
                for (unsigned i = 0; i < neighbor_width_c; ++i)
                {
                    if (0 == (fpresence_c & container()[hash]._flag))
                    { //nothing at this pos
                        container()[hash]._flag |= fpresence_c;
                        container()[hash]._key = key;
                        _count++;
                        return iterator(this, hash);
                    }
                    if (container()[hash]._key == key)
                        return iterator(this, hash);
                    ++hash %= capacity(); //keep in boundary
                }
                return end();
            }
            /**@return number of erased - 0 or 1*/
            unsigned erase(atom_t key)
            {
                const unsigned key_hash = static_cast<unsigned>(key)& bitmask_c;
                unsigned hash = key_hash;
                for (unsigned i = 0; i < neighbor_width_c; ++i)
                {
                    if (0 == (fpresence_c & container()[hash]._flag))
                    { //nothing at this pos
                        return 0;
                    }
                    if (container()[hash]._key == key)
                    {//make erase
                        _count--;
                        //may be rest of neighbors sequence may be shifted by 1, so scan in backward

                        hash = restore_on_erase(hash);
                        //just release pos
                        container()[hash]._flag = 0;
                        return 1;
                    }
                    ++hash %= capacity(); //keep in boundary
                }
                return ~0u;
            }
            void clear()
            {
                std::for_each(container(), container() + size(), [](Content& c){ c._flag = 0; });
                _count = 0;
            }
            Payload& value(const iterator& index)
            {
                //static_assert(!std::is_same(Payload, EmptyPayload), "No value array for this container");
                if (index._offset >= capacity_c
                    || !(container()[index._offset]._flag & fpresence_c))
                    throw std::out_of_range("invalid index or no value associated with key");
                return container()[index._offset]._data;
            }
            /**Find index of key entry or #end() if nothing found*/
            iterator find(atom_t key) const
            {
                unsigned hash = static_cast<unsigned>(key)& bitmask_c;
                for (unsigned i = 0; i < neighbor_width_c; ++i)
                {
                    if (0 == (fpresence_c & container()[hash]._flag))
                    { //nothing at this pos
                        return end();
                    }
                    if (container()[hash]._key == key)
                        return iterator(this, hash);
                    ++hash %= capacity(); //keep in boundary
                }
                return end();
            }
            atom_t key(const iterator& i) const
            {
                return container()[i._offset]._key;
            }
            iterator begin() const
            {
                if (size() == 0)
                    return end();
                for (unsigned i = 0; i < capacity(); ++i)
                    if (container()[i]._flag & fpresence_c)
                        return iterator(this, i);
                return end();
            }
            iterator end() const
            {
                return iterator(this, ~0u);
            }
            iterator& next(iterator& i, typename iterator::distance_type offset = 1) const
            {
                //note following 'for' plays with unsigned-byte arithmetic
                iterator::distance_type inc = offset > 0 ? 1 : -1;
                for (i._offset += inc; offset && i._offset < capacity(); i._offset += inc)
                    if (container()[i._offset]._flag & fpresence_c)
                    {
                        offset -= inc;
                        if (!offset)
                            return i;
                    }
                if (i._offset >= capacity())
                    i._offset = ~0u;
                return i;
            }
            ///
            /// Overrides
            ///
            atom_t allocate_key() override
            {
                for (node_size_t i = 0; i < capacity(); ++i)
                    if (!(container()[i]._flag & fpresence_c))
                    {//occupy this
                        atom_t k = static_cast<atom_t>(i);
                        container()[i]._flag = fpresence_c;
                        container()[i]._key = k;
                        return k;
                    }
                throw std::out_of_range("table is full");
            }
            /**
            *   Get value placeholder for the key
            *   *@throws std::out_of_range exception if key is not exists
            */
            payload_t& value(atom_t key)  override
            {
                auto i = find(key);
                if (i == end())
                    std::out_of_range("no such key");
                return value(i);
            }
            /** Erase the entry associated with key
            *   *@throws std::out_of_range exception if key is not exists
            */
            void remove(atom_t key)  override
            {
                auto n = erase(key);
                if (n == 0)
                    std::out_of_range("no such key");
            }

        private:
            /** Optimize space before some item is removed
            * @return - during optimization this method may change origin param 'erase_pos', so to real erase use index returned
            */
            unsigned restore_on_erase(unsigned erase_pos)
            {
                unsigned erased_hash = static_cast<unsigned>(container()[erase_pos]._key) & bitmask_c;
                unsigned limit = (erase_pos + neighbor_width_c) % capacity(); //start from last available neighbor

                for (unsigned i = (erase_pos + 1) % capacity(); i != limit; ++i %= capacity())
                {
                    if (0 == (fpresence_c & container()[i]._flag))
                        return erase_pos; //stop optimization and erase item at pos
                    unsigned local_hash = (static_cast<unsigned>(container()[i]._key)&bitmask_c);
                    bool item_in_right_place = i == local_hash;
                    if (item_in_right_place)
                        continue;
                    unsigned x = less_pos(erased_hash, erase_pos) ? erase_pos : erased_hash;
                    if (!less_pos(x, local_hash)/*equivalent of <=*/)
                    {
                        copy_to(erase_pos, i);
                        erase_pos = i;
                        erased_hash = local_hash;
                        limit = (erase_pos + neighbor_width_c) % capacity();
                    }
                }
                return erase_pos;
            }
            /** test if tst_min is less than tst_max on condition of cyclyng nature of hash buffer, so (page_c = 8):
                For page-size = 16:
                less(0xF, 0x1) == true
                less(0x1, 0x2) == true
                less(0x1, 0xF) == false
                less(0x2, 0x1) == false
                less(0x5, 0xF) == true
                less(0xF, 0x5) == false
                */
            bool less_pos(unsigned tst_min, unsigned tst_max) const
            {
                int dif = static_cast<int>(tst_min)-static_cast<int>(tst_max);
                unsigned a = std::abs(dif);
                if (a > (static_cast<unsigned>(capacity()) / 2)) //use inversion of signs
                    return dif > 0;
                return dif < 0;
            }
            void copy_to(unsigned to, unsigned src)
            {
                container()[to] = container()[src];
            }
            struct Content
            {
                atom_t _key;
                std::uint8_t _flag;
                Payload _data;
            };
            Content _memory_block[capacity_c];
            node_size_t _count;
        protected:
            Content* container()
            {
                return _memory_block;//reinterpret_cast<Content*>(memory_block());
            }
            const Content* container() const
            {
                return _memory_block;// reinterpret_cast<const Content*>(memory_block());
            }
        };



        template <class Payload, node_size_t capacity>
        struct NodeSortedArray :
            public NavigableByteRangeContainer<Payload>
        {
            typedef NodeSortedArray<Payload, capacity> this_t;
            typedef NodeTableIterator<typename this_t> iterator;
            typedef std::pair<const atom_t*, const atom_t*> key_t;
            enum
            {
                capacity_c = capacity,
                chunk_limit_c = 8
            };
            static size_t allocation_size(size_t capacity)
            {
                return sizeof(chunk_t) * capacity;
            }
            static const std::vector<size_t>& known_capacity()
            {
                static const std::vector<size_t> rv = { 8, 16, 32, 64, 128 };
                return rv;
            }
            NodeSortedArray() :
                _count(0)
            {
            }

            node_size_t size() const
            {
                return _count;
            }
            node_size_t capacity() const
            {
                return capacity_c;
            }
            template <class Str>
            iterator insert(Str&& string)
            {
                return this->insert(string.begin(), string.end());
            }
            /**
            *   @param begin (in/out) - starting pointer of data chunk to store inside container. At exit contains
            *                           information what chunk of data was stored
            */
            template <class T>
            iterator insert(T& begin, T end)
            {
                if (begin == end)
                    return this->end();
                typedef std::iterator_traits<T> traits;
                auto seq_end = container() + this->size();
                if ((end - begin) > chunk_limit_c)
                    end = begin + chunk_limit_c;
                auto str_pair = std::make_pair(begin, end);
                auto inspt = std::lower_bound(container(), seq_end, str_pair, chunk_less<decltype(str_pair.first)>);
                size_t index = inspt - container();

                if (inspt == seq_end)
                {
                    if (size() == capacity()) //no more space to grow
                        return this->end();
                    _count++;
                    set_chunk(index, begin, end);
                    return iterator(this, size() - 1);
                }
                else
                { //something similar has been found

                    int cmp = chunk_cmp(*inspt, str_pair);
                    if (cmp == 0) // element exists
                    {
                        return iterator(this, index);
                    }
                    if (size() == capacity()) //cannot extend array more
                        return this->end();
                    //std::copy_backward(inspt, _key + size(), _key + size()+1);
                    for (auto i = container() + size(); i != inspt; --i)
                        *i = *(i - 1);
                    _count++;
                    set_chunk(index, begin, end);
                    return iterator(this, index);
                }
                //array is over, cannot accomodate item
                return this->end();
            }
            unsigned accommodate(const atom_t*& begin, const atom_t* end) override
            {
                return static_cast<unsigned>(insert(begin, end)._offset);
            }
            void clear()
            {
                for (size_t i = 0; i < size(); ++i)
                    container()[i].all_flags = 0;
                _count = 0;
            }
            iterator end() const
            {
                return iterator(this, ~0u);
            }
            iterator begin() const
            {
                return iterator(this, 0u);
            }

            template <class T>
            iterator find(T begin, T end) const
            {
                auto seq_end = container() + this->size();
                auto str_pair = std::make_pair(begin, end);
                auto fndpt = std::lower_bound(container(), seq_end, str_pair, chunk_less<decltype(str_pair.first)>);
                if (fndpt == seq_end || chunk_cmp(*fndpt, str_pair) != 0)
                    return this->end();
                return iterator(this, fndpt - container());
            }
            template <class Str>
            iterator find(const Str& str) const
            {
                return find(str.begin(), str.end());
            }
            /**
            * Find entry with max approximation to specified prefix.
            * Assume container owns by {"abc", "ac"}, then:
            * \li find_refix('a') == "abc" (lowest is taken)
            * \li find_prefix("b") == end()
            * \li find_prefix("abcde") == "abc"
            *
            * At exite param `begin` points to first unmatched symbol
            */
            template <class T>
            iterator best_match(T& begin, T end) const
            {
                auto seq_end = container() + this->size();
                auto str_pair = std::make_pair(begin, end);
                auto fndpt = std::lower_bound(container(), seq_end, str_pair, chunk_less<decltype(str_pair.first)>);

                if (fndpt == seq_end) //no prefix found
                    return this->end();
                auto prev = fndpt == container() ? seq_end : (fndpt - 1);
                auto result = best_prefix(str_pair, fndpt, prev);
                begin = str_pair.first;
                return result;
            }
            unsigned match_sequence(const atom_t*& begin, const atom_t* end) const override
            {
                return static_cast<unsigned>(best_match(begin, end)._offset);
            }
            iterator& next(iterator& i, typename iterator::distance_type offset = 1) const
            {
                i._offset += offset;
                if (i._offset > size())//note using of unsigned arithmetic
                    i._offset = ~0u;
                return i;
            }

            std::pair<const atom_t*, const atom_t*> key(const iterator& i) const
            {
                if (i._offset < size())
                    return std::make_pair(container()[i._offset].chunk, container()[i._offset].chunk + container()[i._offset].layout.len);
                throw std::out_of_range("invalid index");
            }
            Payload& value(const iterator& i)
            {
                //static_assert(!std::is_same(Payload, EmptyPayload), "No value array for this container");
                if (i._offset >= size())
                    throw std::out_of_range("invalid index");
                return container()[i._offset].data;
            }
            payload_t& value(unsigned key) override
            {
                if (key >= size())
                    throw std::out_of_range("invalid index");
                return container()[key].data;
            }
            const payload_t& value(const iterator& i ) const 
            {
                if (i._offset >= size())
                    throw std::out_of_range("invalid index");
                return container()[i._offset].data;
            }

        private:
            /**
            *   Set text chunk to specified position. Method copies part (not exceeding chunk_limit_c symbols) of source sequence and
            *   retun (as out) chunk_begin where copying was stopped
            *@param entry - index inside _key field
            *@param chunk_begin (in/out) - begin of string sequence, at exit contains position where iteration stopped
            *
            */
            template <class T>
            void set_chunk(size_t entry, T& chunk_begin, T chunk_end)
            {
                size_t i = 0;
                chunk_t& v = container()[entry];
                for (; i < chunk_limit_c && chunk_begin != chunk_end; ++chunk_begin, ++i)
                    v.chunk[i] = *chunk_begin;
                v.layout.len = i;
                v.layout.flag = fpresence_c; //not |= because no value yet
            }
            struct chunk_t
            {
                union
                {
                    std::uint8_t all_flags;
                    struct
                    {
                        std::uint8_t flag : 3;
                        std::uint8_t len : 5;
                    } layout;
                };
                atom_t chunk[chunk_limit_c];
                Payload data;
            };
            chunk_t _memory_block[capacity_c];
            node_size_t _count;
            template <class T>
            static bool chunk_less(const chunk_t& left, std::pair<T, T> right)
            {
                return chunk_cmp(left, right) < 0;
            }
            /**
            * Provide lexicographic compare
            * @param right on output right.first will contain first un-equal symbol
            * @return <0 if left less than right, == 0 if less == right and >0 if right > left
            */
            template <class Pair>
            static int chunk_cmp(const chunk_t& left, Pair& right)
            {
                size_t i = 0;
                for (; i < left.layout.len && right.first != right.second; ++i, ++right.first)
                {
                    if (left.chunk[i] < *right.first)
                        return -1;
                    if (*right.first < left.chunk[i])
                        return 1;
                    //equals goes to continue
                }
                //there since prefixes are lexicaly equal 
                return i < left.layout.len ? 1 // 'left' is longer
                    : (right.first != right.second ? -1 // 'right' is longer
                    : 0);
            }
            template<class T>
            iterator best_prefix(T& key_pair, const chunk_t* first, const chunk_t* second) const
            {
                T a = key_pair;
                int diff1 = prefix_len(first, key_pair);
                int diff2 = prefix_len(second, a);
                if (diff1 < diff2)
                {//second is better
                    key_pair.first = a.first;
                    return iterator(this, second - container());
                }

                return diff1 == 0 ? end() : iterator(this, first - container());
            }
            template<class T>
            int prefix_len(const chunk_t* i, std::pair<T, T>& key_pair) const
            {
                T begin = key_pair.first;
                const chunk_t* end = container() + size();
                if (i == end) //no prefix found
                    return 0; //bad prefix
                //returned value per doc:  is not less than (i.e. greater or equal to) value.
                chunk_cmp(*i, key_pair); //need to position str_pair
                return static_cast<int>(key_pair.first - begin);
            }
        protected:
            chunk_t * container()
            {
                return _memory_block;//reinterpret_cast<chunk_t*>(memory_block());
            }
            const chunk_t * container() const
            {
                return _memory_block;//reinterpret_cast<const chunk_t*>(memory_block());
            }
        };

        template <class Payload>
        struct NodeTrie :
            public NavigableByteRangeContainer<Payload>
        {
            enum
            {
                page_c = 256,
                capacity_c = page_c
            };
            NodeTrie()
                :flag{}
            {

            }
            unsigned accommodate(const atom_t*& begin, const atom_t* end) override
            {
                if (begin == end)
                    return ~0u;
                auto k = *begin++;
                if (!(flag[k] & fpresence_c))
                {
                    //++head()._count;
                    flag[k] |= fpresence_c;
                }
                return k;
            }
            unsigned match_sequence(const atom_t*& begin, const atom_t* end) const override
            {
                if (begin == end)
                    return ~0u;

                auto k = *begin;
                if (flag[k] & fpresence_c)
                {
                    ++begin;
                    return k;
                }
                return ~0u;
            }

        private:
            atom_t flag[capacity_c];
        };

    }//endof namespace trie
}//endof namespace OP

#endif //_OP_CONTAINERS__H_
