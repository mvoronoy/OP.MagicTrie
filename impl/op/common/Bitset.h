#ifndef _OP_TRIE_BITSET__H_
#define _OP_TRIE_BITSET__H_
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <assert.h>
namespace OP
{
    namespace trie
    {

        /**
        *   Origin from http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
        */
        inline std::uint32_t log2_32(std::uint32_t v)
        {
            static const std::uint8_t MultiplyDeBruijnBitPosition[32] =
            {
                0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
                8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
            };

            v |= v >> 1; // first round down to one less than a power of 2 
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;

            return MultiplyDeBruijnBitPosition[static_cast<std::uint32_t>(v * 0x07C4ACDDU) >> 27];
        }
        inline std::uint32_t log2_64(std::uint64_t v)
        {
            return v > 0x00000000FFFFFFFFul
                ? log2_32(static_cast<std::uint32_t>(v >> 32)) + 32
                : log2_32(static_cast<std::uint32_t>(v))
                ;
        }
        /**
        *   Origin from http://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
        */
        inline std::uint32_t count_trailing_zero_32(std::uint32_t v)
        {
            static const std::uint8_t MultiplyDeBruijnBitPosition[32] =
            {
                0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
                31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
            };
            return MultiplyDeBruijnBitPosition[static_cast<std::uint32_t>(((v & (~v+1)) * 0x077CB531U)) >> 27];
        }
        inline std::uint32_t count_trailing_zero_64(std::uint64_t v)
        {
            return (v & 0x00000000FFFFFFFFul)
                ? count_trailing_zero_32(static_cast<std::uint32_t>(v))
                : count_trailing_zero_32(static_cast<std::uint32_t>(v >> 32)) + 32
                ;
        }
        template <size_t N, class Int>
        struct BitsetIterator 
        {
            using iterator_category = std::random_access_iterator_tag;
            enum
            {
                /**bits count in single entry*/
                bits_c = sizeof(Int) << 3,
                /**Total bit count managed by this container*/
                bit_length_c = bits_c * N
            };
            using difference_type = ptrdiff_t;
            using this_t = BitsetIterator<N, Int>;

            BitsetIterator() : _ptr(nullptr), _offset{} {}

            BitsetIterator(const Int* rhs, size_t offset)
                : _ptr(rhs)
                , _offset(offset) {}

            inline this_t& operator+=(difference_type rhs)
            {
                _offset += rhs;
                return *this;
            }
            inline this_t& operator-=(difference_type rhs)
            {
                _offset -= rhs;
                return *this;
            }
            inline bool operator*() const
            {
                return operator[](_offset);
            }
            inline bool operator[](size_t index) const
            {
                assert(index < bit_length_c);
                assert(_ptr);
                return (_ptr[(N - 1) - index / bits_c] & (1ULL << (index % bits_c))) != 0;
            }

            inline this_t& operator++()
            {
                ++_offset;
                return *this;
            }
            inline this_t& operator--()
            {
                --_offset;
                return *this;
            }
            inline this_t operator++(int)
            {
                this_t tmp(*this);
                --_offset;
                return tmp;
            }
            inline this_t operator--(int)
            {
                this_t tmp(*this);
                --_ptr;
                return tmp;
            }
            /* inline Iterator operator+(const Iterator& rhs) {return Iterator(_ptr+rhs.ptr);} */
            inline difference_type operator-(const this_t& rhs) const
            {
                assert(_ptr == rhs._ptr);
                return difference_type(_offset - rhs.offset);
            }
            inline this_t operator+(difference_type rhs) const
            {
                this_t result(*this);
                result._offset += rhs;
                return result;
            }
            inline this_t operator-(difference_type rhs) const
            {
                this_t result(*this);
                result._offset -= rhs;
                return result;
            }
            friend inline this_t operator+(difference_type lhs, const this_t& rhs)
            {
                this_t result(rhs);
                result._offset += lhs;
                return result;
            }
            friend inline this_t operator-(difference_type lhs, const this_t& rhs)
            {
                this_t result(rhs);
                result._offset = lhs - result._offset;
                return result;
            }

            inline bool operator==(const this_t& rhs) const
            {
                return  _offset == rhs._offset;
            }
            inline bool operator!=(const this_t& rhs) const
            {
                return _offset != rhs._offset;
            }
            inline bool operator>(const this_t& rhs) const
            {
                return _offset > rhs._offset;
            }
            inline bool operator<(const this_t& rhs) const
            {
                return _offset < rhs._offset;
            }
            inline bool operator>=(const this_t& rhs) const
            {
                return _offset >= rhs._offset;
            }
            inline bool operator<=(const this_t& rhs) const
            {
                return _offset <= rhs._offset;
            }
        private:
            const Int* _ptr;
            size_t _offset;
        };
        
        /**Allows iterate over Bitset only for entries that are set*/
        template <class TBitset>
        struct PresenceIterator 
        {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = typename TBitset::dim_t;
            

            typedef PresenceIterator<TBitset> this_t;

            explicit PresenceIterator(const TBitset *owner)
                : _owner(owner)
                , _pos(owner->first_set())
            {

            }
            PresenceIterator()
                : _owner(nullptr)
                , _pos(TBitset::nil_c)
            {

            }
            inline typename TBitset::dim_t operator*() const
            {
                return _pos;
            }
            inline this_t& operator ++ ()
            {
                assert(_pos != TBitset::nil_c);
                _pos = _owner->next_set(_pos);
                return *this;
            }
            inline this_t operator ++ (int)
            {
                auto rv = *this;
                this->operator++();
                return rv;
            }
            inline bool operator==(const this_t& rhs) const
            {
                return _pos == rhs._pos;
            }
            inline bool operator!=(const this_t& rhs) const
            {
                return _pos != rhs._pos;
            }
            inline bool operator>(const this_t& rhs) const
            {
                return _pos > rhs._pos;
            }
            inline bool operator<(const this_t& rhs) const
            {
                return _pos < rhs._pos;
            }
            inline bool operator>=(const this_t& rhs) const
            {
                return _pos >= rhs._pos;
            }
            inline bool operator<=(const this_t& rhs) const
            {
                return _pos <= rhs._pos;
            }
        private:
            typename TBitset::dim_t _pos;
            const TBitset *_owner;
        };
        /**Represent bit-set.*/
        template <size_t N = 1, typename Int = std::uint64_t>
        struct Bitset
        {
            friend struct Bitset<N + 1, Int>;
            using this_t = Bitset<N, Int>;

            using const_iterator = BitsetIterator<N, Int>;
            using const_presence_iterator = PresenceIterator<this_t>;

            typedef std::uint16_t dim_t;
            typedef std::uint8_t atom_t;
            static const dim_t nil_c = dim_t(~0u);
            enum
            {
                /**bits count in single entry*/
                bits_c = const_iterator::bits_c,
                /**Total bit count managed by this container*/
                bit_length_c = const_iterator::bit_length_c
            };
            explicit Bitset(Int def = {0})
            {
                std::fill_n(_presence, N, def);
            }
            const_iterator begin() const
            {
                return const_iterator(_presence, 0);
            }
            const_iterator end() const
            {
                return const_iterator(_presence, bit_length_c);
            }
            const_presence_iterator presence_begin() const
            {
                return const_presence_iterator(this);
            }
            const_presence_iterator presence_end() const
            {
                return const_presence_iterator();
            }
            /**@return index of first bit that is set, or `nil_c` if no bits*/
            inline dim_t first_set() const
            {
                return presence_index(_presence);
            }
            inline dim_t last_set() const
            {
                return revert_presence_index(_presence);
            }
            /**Return index of bit that is set after 'prev' one. May return `nil_c` if no bits are set.*/
            inline dim_t next_set(dim_t prev) const
            {
                Int mask = (1ULL << (prev % bits_c));
                mask |= mask - 1;
                for (auto i = prev / bits_c; i < N; ++i)
                {
                    auto x = _presence[i] & ~mask;
                    if (x != 0) //test all bits are set
                    {
                        return static_cast<dim_t>(count_trailing_zero_64(x) + i*bits_c);
                    }
                    mask = Int(0); //reset mask for all other entries
                }
                return nil_c;
            }
            /**Return index of set bit that is equal or follow after 'prev'. May return `nil_c` if no bits are set.*/
            inline dim_t next_set_or_this(dim_t prev) const
            {
                Int mask = (1ULL << (prev % bits_c)) - 1;
                for (auto i = prev / bits_c; i < N; ++i)
                {
                    auto x = _presence[i] & ~mask;
                    if (x != 0) //test all bits are set
                    {
                        return static_cast<dim_t>(count_trailing_zero_64(x) + i*bits_c);
                    }
                    mask = Int(0); //reset mask for all other entries
                }
                return nil_c;
            }
            /**Return index of bit that is set prior 'index' one. May return `nil_c` if no bits are set prior index.*/
            inline dim_t prev_set(dim_t index) const
            {
                Int mask = (1ULL << (index % bits_c)) - 1;
                for (int i = index / bits_c; i >=0; --i)
                {
                    auto x = _presence[i] & mask;
                    if (x != 0) //test all bits are set
                    {
                        return static_cast<dim_t>(ln2(x) + i*bits_c);
                    }
                    mask = ~Int(0); //reset mask for all other entries
                }
                return nil_c;
            }
            /**Return index of first bit that is not set*/
            inline dim_t first_clear() const
            {
                return clearence_index(_presence);
            }
            inline bool get(dim_t index)const
            {
                assert(index < bit_length_c);
                return 0 != (_presence[index / bits_c] & ( 1ULL << (index % bits_c) ));
            }
            inline void set(dim_t index)
            {
                assert(index < bit_length_c);
                _presence[index / bits_c] |= 1ULL << (index % bits_c);
            }
            inline void clear(dim_t index)
            {
                assert(index < bit_length_c);

                _presence[index / bits_c] &= ~(1ULL << (index % bits_c));
            }
            inline void toggle(dim_t index)
            {
                assert(index < bit_length_c);
                _presence[index / bits_c] ^= (1ULL << (index % bits_c));
            }
            size_t capacity() const
            {
                return bit_length_c;
            }
        private:
            static inline dim_t ln2(std::uint_fast64_t value)
            {
                return log2_64(value);
            }
            static inline dim_t presence_index(const std::uint64_t presence[N])
            {
                for (auto i = 0; i < N; ++i)
                {
                    if (presence[i] != 0) //test all bits are set
                    {
                        return static_cast<dim_t>(count_trailing_zero_64(presence[i]) + i*bits_c);
                    }
                }
                return nil_c;
            }
            /**Find position of lowest bit-set*/
            static inline dim_t revert_presence_index(const std::uint64_t presence[N])
            {
                for (auto i = N; i > 0; --i)
                {
                    auto j = i - 1u;
                    if (presence[j] != 0) //test all bits are set
                    {
                        return static_cast<dim_t>(ln2(presence[j]) + j*bits_c);
                    }
                }
                return nil_c;
            }
            /**detect first un-set bit*/
            static inline dim_t clearence_index(const std::uint64_t presence[N])
            {   
                //note 1: to test first clear bit uses inversion (~) so first set bit is detected
                //note 2: that ( x & (~(x) + 1) ) deletes all but the lowest set bit
                for (auto i = 0; i < N; ++i)
                {
                    if (presence[i] != std::numeric_limits<std::uint64_t>::max()) //test all bits are set
                    {
                        return ln2(~presence[i] & (presence[i] + 1)) + i*bits_c;
                    }
                }
                return nil_c;
            }
        private:
            Int _presence[N];
        };
        
    } //ns: trie
}//ns:OP
#endif //_OP_TRIE_BITSET__H_
