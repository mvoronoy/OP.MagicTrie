#ifndef _OP_TRIE_BITSET__H_
#define _OP_TRIE_BITSET__H_
#include <cstdint>
#include <cstdlib>
#include <iterator>
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

        template <size_t N, class Int>
        struct BitsetIterator : public std::iterator<std::random_access_iterator_tag, bool>
        {

            enum
            {
                /**bits count in single entry*/
                bits_c = sizeof(Int) << 3,
                /**Total bit count managed by this container*/
                bit_length_c = bits_c * N
            };

            using base_t = typename std::iterator<std::random_access_iterator_tag, bool>;
            using difference_type = typename base_t::difference_type;
            using this_t = typename BitsetIterator<N, Int>;

            BitsetIterator() : _ptr(nullptr), _offset{} {}

            BitsetIterator(const Int* rhs, size_t offset)
                : _ptr(rhs),
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

        template <size_t N = 1, typename Int = std::uint64_t>
        struct Bitset
        {
            friend struct Bitset<N + 1, Int>;
            using const_iterator = BitsetIterator<N, Int>;
            
            typedef std::uint16_t dim_t;
            typedef std::uint8_t atom_t;

            enum
            {
                /**bits count in single entry*/
                bits_c = const_iterator::bits_c,
                /**Total bit count managed by this container*/
                bit_length_c = const_iterator::bit_length_c
            };
            Bitset(std::uint64_t def = 0ULL)
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
            /**Return index of first bit that is set*/
            inline const_iterator first_set() const
            {
                return presence_index(_presence);
            }
            /**Return index of first bit that is not set*/
            inline dim_t first_clear() const
            {
                return clearence_index(_presence);
            }
            inline void set(dim_t index)
            {
                assert(index < bit_length_c);
                _presence[(N - 1) - index / bits_c] |= 1ULL << (index % bits_c);
            }
            inline void clear(dim_t index)
            {
                assert(index < bit_length_c);

                _presence[(N - 1) - index / bits_c] &= ~(1ULL << (index % bits_c));
            }
            inline void toggle(dim_t index)
            {
                assert(index < bit_length_c);
                _presence[(N - 1) - index / bits_c] ^= (1ULL << (index % bits_c));
            }
        private:
            static inline dim_t ln2(std::uint_fast64_t value)
            {
                return log2_64(value);
            }
            static inline dim_t presence_index(const std::uint64_t presence[N])
            {
                return presence[0] == 0
                    ? Presence256<N - 1>::presence_index(presence + 1)
                    : ln2(presence[0]) + (N - 1)*bits_c;
            }
            static inline dim_t clearence_index(const std::uint64_t presence[N])
            {
                return presence[0] == std::numeric_limits<uint64_t>::max()
                    ? Presence256<N - 1>::clearence_index(presence + 1)
                    : ln2(~presence[0]) + (N - 1)*bits_c;
            }
        private:
            Int _presence[N];
        };
        template <>
        inline Bitset<1>::dim_t Bitset<1>::presence_index(const std::uint64_t presence[1])
        {
            return presence[0] == 0
                ? ~dim_t(0) //no entry
                : ln2(presence[0]);
        }
        template <>
        inline Bitset<1>::dim_t Bitset<1>::clearence_index(const std::uint64_t presence[1])
        {
            return presence[0] == std::numeric_limits<uint64_t>::max()
                ? ~dim_t(0) //no entry
                : ln2(~presence[0]);
        }
    } //ns: trie
}//ns:OP
#endif //_OP_TRIE_BITSET__H_
