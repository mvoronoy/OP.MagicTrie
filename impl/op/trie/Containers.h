#ifndef _OP_CONTAINERS__H_
#define _OP_CONTAINERS__H_

#ifdef _MSC_VER
#pragma warning( disable : 4351) //stupidies warning from VS /new behavior: elements of array 'array' will be default initialized/
#endif //_MSC_VER

#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <array>
#include <op/vtm/typedefs.h>

namespace OP::trie
{
    /**unsigned size of single page(container) */
    using node_size_t = std::uint16_t;

    namespace _internal
    {
        
        template <node_size_t node_size>
        constexpr inline node_size_t max_hash_neighbors = 
                    (node_size == 8) 
                    ? 2
                    : ((node_size == 16 || node_size == 32)
                        ? 3
                        : ((node_size == 64 || node_size == 128 || node_size == 256) ? 4 : 0)
                      )
                    ;
    }

    /**
    *   base for persistence containers that can store some byte range
    */
    template <class Payload>
    struct NavigableByteRangeContainer
    {
        using payload_t = Payload;
        using atom_t = OP::common::atom_t;

        virtual ~NavigableByteRangeContainer() = default;
        /**
        *   Method inserts some part of source sequence (specified by [begin, end) ). Caller can check how
        *   many bytes were inserted by checking in/out parameter `begin`. For example you have source sequence "abcdef",
        *   for some reason container can only accommodate "abc" then insert will be successful for "abc" and `begin` will point to "def"
        *   @param begin - (in/out) for input it is begin of sequence to insert, on out the first non-inserted character
        *   @param end - end of source sequence
        *   @return :
        *   \li if item is unique and there is container space - index inside container where insertion were done.
        *   \li if item already exists - index inside container where the item is placed (to distinct from prev. check that `begin` wasn't modified).
        *   \li if container cannot grow and accommodate item result is ~0u (unsigned presentation of -1).
        */
        virtual unsigned accommodate(const atom_t*& begin, const atom_t* end) = 0;

        /**
        * Find entry with max approximation to specified prefix.
        * Assume container owns by {"abc", "cd"}, then:
        * \li find_refix('a') == "abc" (lowest is taken)
        * \li find_prefix("b") == end()
        * \li find_prefix("abcde") == "abc"
        *
        * At exit param `begin` points to first unmatched symbol
        */
        virtual unsigned match_sequence(const atom_t*& begin, const atom_t* end) const = 0;
        
        virtual payload_t& value(unsigned key) = 0;
        //virtual const payload_t& value(unsigned key) const = 0;
    };

    /** base for containers that implements byte-key to value contract */
    template <class Payload>
    struct ByteKeyContainer
    {
        using payload_t = Payload;
        using atom_t = OP::common::atom_t;

        virtual ~ByteKeyContainer() = default;

        /** Render and allocate unique key that is not exists in this container yet
        */
        virtual atom_t allocate_key() noexcept = 0;
        
        /**
        *   Get value placeholder for the key
        */
        virtual payload_t& value(atom_t key) noexcept = 0;
    
        /** Erase the entry associated with key
        */
        virtual void remove(atom_t key) noexcept = 0;
    };

    template <class Container>
    struct NodeTableIterator
    {
        using container_t = Container;

#ifdef __MINGW32__
        friend class Container;
#else
        friend container_t;
#endif // __MINGW32__
        using atom_t = OP::common::atom_t;
        using this_t = NodeTableIterator<Container>;
        using iterator_category = std::random_access_iterator_tag;
        using value_type = typename Container::key_t;
        using difference_type = std::ptrdiff_t;
        using distance_type = std::ptrdiff_t;
        using pointer = atom_t*;
        using reference = atom_t&;

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
        this_t operator + (distance_type value) const
        {
            this_t rv(*this);
            return _container->next(rv, value);
        }
        this_t operator - (distance_type value) const
        {
            this_t rv(*this);
            return _container->next(rv, -value);
        }
        
        distance_type operator - (const this_t & other) const
        {
            this_t rv(*this);
            return _offset - other._offset;
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
        fpresence_c = 0x01,
        faddress_c = 0x02,
        fvalue_c = 0x04,
        _f_userdefined_ = 0x08,
        fdef_0  = _f_userdefined_,
        fdef_1  = _f_userdefined_ << 1,
        fdef_2  = _f_userdefined_ << 2,
        fdef_3  = _f_userdefined_ << 3,
        fdef_4  = _f_userdefined_ << 4
    };

    template <class Payload, node_size_t TCapacity>
    struct NodeHashTable : public ByteKeyContainer<Payload>
    {
        using base_t = ByteKeyContainer<Payload>;
        using payload_t = typename base_t::payload_t;

        constexpr static inline node_size_t capacity_c = TCapacity;
        constexpr static inline node_size_t bitmask_c = capacity_c - 1; /*on condition capacity is power of 2*/
        constexpr static inline node_size_t neighbor_width_c = _internal::max_hash_neighbors<capacity_c>;

        using this_t = NodeHashTable<Payload, capacity_c>;
        using iterator = NodeTableIterator<this_t>;
        using key_t = OP::common::atom_t;

        constexpr NodeHashTable() noexcept
            : _count(0)
            , _memory_block{}
        {
            static_assert(_internal::max_hash_neighbors<capacity_c> != 0, "Allowed hashtables only with page size 8,16,32,64,128");
        }

        constexpr node_size_t size() const noexcept
        {
            return _count;
        }

        constexpr bool empty() const noexcept
        {
            return size() == 0;
        }

        node_size_t capacity() const noexcept
        {
            return capacity_c;
        }

        /**
        *   @return insert position or #end() if no more capacity
        */
        std::pair<iterator, bool> insert(key_t key) noexcept
        {
            unsigned hash = static_cast<unsigned>(key)& bitmask_c;
            for (unsigned i = 0; i < neighbor_width_c; ++i)
            {
                if (0 == (fpresence_c & container()[hash]._flag))
                { //nothing at this pos
                    container()[hash]._flag |= fpresence_c;
                    container()[hash]._key = key;
                    _count++;
                    return std::make_pair(iterator(this, hash), true);
                }
                if (container()[hash]._key == key)
                    return std::make_pair(iterator(this, hash), false); //already exists
                ++hash %= capacity(); //keep in boundary
            }
            return std::make_pair(end(), false); //no capacity
        }
        
        /**@return number of erased - 0 or 1*/
        unsigned erase(key_t key) noexcept
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
        
        void clear() noexcept
        {
            std::for_each(container(), container() + size(), [](Content& c){ c._flag = 0; });
            _count = 0;
        }

        payload_t& value(const iterator& index) noexcept
        {
            //"invalid index or no value associated with key"
            assert(index._offset >= capacity_c
                || !(container()[index._offset]._flag & fpresence_c));

            return container()[index._offset]._data;
        }

        /**Find index of key entry or #end() if nothing found*/
        iterator find(key_t key) const noexcept
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

        key_t key(const iterator& i) const noexcept
        {
            return container()[i._offset]._key;
        }
        
        iterator begin() const noexcept
        {
            if (size() == 0)
                return end();
            for (unsigned i = 0; i < capacity(); ++i)
                if (container()[i]._flag & fpresence_c)
                    return iterator(this, i);
            return end();
        }
        
        iterator end() const noexcept
        {
            return iterator(this, ~0u);
        }

        iterator& next(iterator& i, typename iterator::distance_type offset = 1) const noexcept
        {
            //note following 'for' plays with unsigned-byte arithmetic
            typename iterator::distance_type inc = offset > 0 ? 1 : -1;
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
        key_t allocate_key() noexcept override
        {
            node_size_t i = 0;
            for (; i < capacity(); ++i)
                if (!(container()[i]._flag & fpresence_c))
                {//occupy this
                    break;
                }
            assert(i < capacity());//"table is full"!;

            key_t k = static_cast<key_t>(i);
            container()[i]._flag = fpresence_c;
            container()[i]._key = k;
            return k;
        }

        /**
        *   Get value placeholder for the key
        */
        payload_t& value(key_t key) noexcept override
        {
            auto i = find(key);
            assert(i != end());//throw std::out_of_range("no such key");
            return value(i);
        }

        /** Erase the entry associated with key
        *   *@throws std::out_of_range exception if key is not exists
        */
        void remove(key_t key) noexcept override
        {
            auto n = erase(key);
            assert(n > 0);// throw std::out_of_range("no such key");
        }

    private:
        /** Optimize space before some item is removed
        * @return - during optimization this method may change origin param 'erase_pos', so to real erase use index returned
        */
        unsigned restore_on_erase(unsigned erase_pos) noexcept
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
        bool less_pos(unsigned tst_min, unsigned tst_max) const noexcept
        {
            int dif = static_cast<int>(tst_min)-static_cast<int>(tst_max);
            unsigned a = std::abs(dif);
            if (a > (static_cast<unsigned>(capacity()) / 2)) //use inversion of signs
                return dif > 0;
            return dif < 0;
        }

        void copy_to(unsigned to, unsigned src) noexcept
        {
            container()[to] = container()[src];
        }

        struct Content
        {
            key_t _key;
            std::uint8_t _flag;
            Payload _data;
        };

        Content _memory_block[capacity_c];
        node_size_t _count;

    protected:
        
        Content* container() noexcept
        {
            return _memory_block;//reinterpret_cast<Content*>(memory_block());
        }

        const Content* container() const noexcept
        {
            return _memory_block;// reinterpret_cast<const Content*>(memory_block());
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
        using atom_t = OP::common::atom_t;
        
        NodeTrie()
            :flag{}
        {

        }

        unsigned accommodate(const atom_t*& begin, const atom_t* end) noexcept override
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

        unsigned match_sequence(const atom_t*& begin, const atom_t* end) const noexcept override
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

}//endof namespace OP::trie

#endif //_OP_CONTAINERS__H_
