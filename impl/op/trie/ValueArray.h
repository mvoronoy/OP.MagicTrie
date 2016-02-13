#ifndef _OP_TRIE_VALUEARRAY__H_
#define _OP_TRIE_VALUEARRAY__H_

#include <OP/common/typedefs.h>
#include <OP/vtm/SegmentManager.h>

namespace OP
{
    namespace trie
    {
        struct EmptyPayload{};

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
                other.presence = no_data_c; //clear previous
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
                , presence(no_data_c)
            {}
            ValueArrayData& operator = (this_t && other) OP_NOEXCEPT
            {
                child = other.child;
                data = std::move(other.data); //in hope that payload supports move
                presence = other.presence;
                other.presence = no_data_c; //clear previous
                return *this;
            }
            void clear()
            {
                data.~Payload();
                child = FarAddress();
                presence = no_data_c;
            }
            FarAddress get_child() const
            {
                return presence & has_child_c ? child : FarAddress();
            }
            /**Set new child. This method also modifies flag #has_child_c if address is not SegmentDef::far_null_c */
            void set_child(FarAddress address) 
            {
                if (address.address == SegmentDef::far_null_c)
                {
                    presence &= ~has_child_c;
                }
                else
                {
                    presence |= has_child_c;
                }
                child = address;
            }
            /**Set new value. This method also uncodintionally modifies flag #has_data_c. To clear data use #clear_data() */
            void set_data(payload_t && apayload)
            {
                data = std::move(apayload);
                presence |= has_data_c;
            }
            void clear_data()
            {
                presence &= ~has_data_c;
            }

            bool has_child() const
            {
                return 0 != (presence & has_child_c);
            }
            bool has_data() const
            {
                return 0 != (presence & has_data_c);
            }

            enum : std::uint8_t
            {
                no_data_c   = 0,
                has_child_c = 0x1,
                has_data_c  = 0x2
            };

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
                auto& memmngr = _topology.slot<MemoryManager>();
                //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                auto result = memmngr.make_array<vad_t>(capacity, std::forward<payload_t>(payload));
                //g.commit();
                return PersistedArray<vad_t>(result);
            }
            /**Destroy previously allocated by #create() */
            void destroy(const PersistedArray<vad_t>& array_ref)
            {
                auto& memmngr = _topology.slot<MemoryManager>();
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