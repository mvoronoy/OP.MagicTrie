#ifndef _OP_TRIE_VALUEARRAY__H_
#define _OP_TRIE_VALUEARRAY__H_

#include <OP/common/typedefs.h>
#include <OP/vtm/SegmentManager.h>

namespace OP
{
    namespace trie
    {
        struct EmptyPayload{};
        
        enum Terminality: std::uint8_t
        {
            term_no = 0,
            term_has_child = 0x1,
            term_has_data = 0x2
        };
        inline Terminality operator & (Terminality left, Terminality right)
        {
            return static_cast<Terminality>(  static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right) );
        }
        inline Terminality operator | (Terminality left, Terminality right)
        {
            return static_cast<Terminality>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
        }
        inline Terminality& operator &= (Terminality& left, Terminality right)
        {
            left = left & right;
            return left;
        }
        inline Terminality& operator |= (Terminality& left, Terminality right)
        {
            left = left | right;
            return left;
        }
        template <class T>
        inline bool is_set(T value, Terminality test)
        {
            return (value & static_cast<T>(test)) == test;
        }
        template <class T>
        inline bool is_not_set(T value, Terminality test)
        {
            return (value & static_cast<T>(test)) != test;
        }
        template <class Payload>
        struct ValueArrayData
        {
            typedef Payload payload_t;
            typedef ValueArrayData<payload_t> this_t;

            //static_assert(std::is_standard_layout<this_t>::value, "ValueArrayData<T> must be a standard-layout");

            ValueArrayData(this_t && other) OP_NOEXCEPT
                : child(other.child)
                , data(std::move(other.data)) //in hope that payload supports move
                , presence(other.presence)
            {
                other.presence = term_no; //clear previous
            }
            ValueArrayData(const this_t & other) OP_NOEXCEPT
                : child(other.child)
                , data(other.data) //in hope that payload supports move
                , presence(other.presence)
            {
            }
            ValueArrayData(payload_t && apayload = payload_t()) OP_NOEXCEPT
                : child(SegmentDef::far_null_c)
                , data(std::forward<payload_t>(apayload))
                , presence(term_no)
            {}
            ValueArrayData& operator = (this_t && other) OP_NOEXCEPT
            {
                child = other.child;
                data = std::move(other.data); //in hope that payload supports move
                presence = other.presence;
                other.presence = term_no; //clear previous
                return *this;
            }
            void clear()
            {
                data.~Payload();
                child = FarAddress();
                presence = term_no;
            }
            FarAddress get_child() const
            {
                return presence & term_has_child ? child : FarAddress();
            }
            /**Set new child. This method also modifies flag #term_has_child if address is not SegmentDef::far_null_c */
            void set_child(FarAddress address) 
            {
                if (address.address == SegmentDef::far_null_c)
                {
                    presence &= ~term_has_child;
                }
                else
                {
                    presence |= term_has_child;
                }
                child = address;
            }
            /**Set new value. This method also uncodintionally modifies flag #term_has_data. To clear data use #clear_data() */
            void set_data(payload_t && apayload)
            {
                data = std::move(apayload);
                presence |= term_has_data;
            }
            void clear_data()
            {
                data.~Payload();
                presence &= ~term_has_data;
            }

            bool has_child() const
            {
                return 0 != (presence & term_has_child);
            }
            bool has_data() const
            {
                return 0 != (presence & term_has_data);
            }

            /**Reference to dependent children*/
            FarAddress child;
            std::uint8_t presence;
            Payload data;
        };

        template <class SegmentTopology, class Payload>
        struct ValueArrayManager
        {
            typedef ValueArrayManager<SegmentTopology, Payload> this_t;
            typedef Payload payload_t;
            typedef ValueArrayData<payload_t> vad_t;

            ValueArrayManager(SegmentTopology& topology)
                    : _topology(topology)
                {}
            
            PersistedArray<vad_t> create(dim_t capacity, payload_t && payload = payload_t())
            {
                auto& memmngr = _topology.slot<HeapManagerSlot>();
                //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                auto result = memmngr.make_array<vad_t>(capacity, std::forward<payload_t>(payload));
                //g.commit();
                return PersistedArray<vad_t>(result);
            }
            /**Destroy previously allocated by #create() */
            void destroy(const PersistedArray<vad_t>& array_ref)
            {
                auto& memmngr = _topology.slot<HeapManagerSlot>();
                memmngr.deallocate(array_ref.address);
            }

            struct MoveProcessor
            {
                friend struct this_t;
                
                void move(dim_t from, dim_t to)
                {
                    auto v = _source[from];
                    _dest[to] = std::move(v);
                }
                PersistedArray<vad_t> dest_addr() const
                {
                    return _dest_addr;
                }
            private:
                MoveProcessor(ReadonlyAccess<vad_t>&&source, WritableAccess<vad_t>&& dest, PersistedArray<vad_t> dest_addr) 
                    : _source(std::move(source))
                    , _dest(std::move(dest))
                    , _dest_addr(dest_addr)
                {}
                ReadonlyAccess<vad_t> _source;
                WritableAccess<vad_t> _dest;
                PersistedArray<vad_t> _dest_addr;
            };
            /**Allocate new array and move items from source one by reindexing rules*/
            MoveProcessor grow(PersistedArray<vad_t> source, fast_dim_t old_capacity, fast_dim_t new_capacity)
            {
                auto dest = create(new_capacity);
                auto source_arr_view = array_view<vad_t>(_topology, source.address, old_capacity);
                auto dest_view = array_accessor<vad_t>(_topology, dest.address, new_capacity);
                return MoveProcessor(std::move(source_arr_view), std::move(dest_view), dest);
            }
            WritableAccess<vad_t> accessor(const PersistedArray<vad_t>& array_ref, dim_t capacity)
            {
                return array_accessor<vad_t>(_topology, array_ref.address, capacity);
            }
            ReadonlyAccess<vad_t> view(const PersistedArray<vad_t>& array_ref, dim_t capacity) const
            {
                return array_view<vad_t>(_topology, array_ref.address, capacity);
            }
        private:
            SegmentTopology& _topology;
        };
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_VALUEARRAY__H_