#ifndef _OP_TRIE_TRIENODE__H_
#define _OP_TRIE_TRIENODE__H_

#include <op/trie/HashTable.h>
#include <op/trie/StemContainer.h>
#include <op/trie/ValueArray.h>
#include <op/vtm/PersistedReference.h>
#include <op/common/Bitset.h>

namespace OP
{
    namespace trie
    {
        struct NodeUid
        {
            enum
            {
                size_c = 16
            };
            std::uint8_t uid[size_c];
        };
        inline bool operator == (const NodeUid& left, const NodeUid& right)
        {
            return memcmp(left.uid, right.uid, NodeUid::size_c) == 0;
        }
        inline bool operator != (const NodeUid& left, const NodeUid& right)
        {
            return !(left == right);
        }
        /** Represent single node of Trie*/
        template <class Payload>
        struct TrieNode
        {
            typedef typename Payload payload_t;
            typedef TrieNode<payload_t> this_t;

            /*declare 256-bit presence bitset*/
            typedef Bitset<4, std::uint64_t> presence_t;

            typedef std::uint32_t version_t;
            typedef OP::trie::containers::HashTableData reindex_hash_t;
            typedef PersistedReference<reindex_hash_t> ref_reindex_hash_t;
            
            typedef OP::trie::stem::StemData stems_t;
            typedef PersistedReference<stems_t> ref_stems_t;

            typedef ValueArrayData<payload_t> values_t;
            typedef PersistedArray<values_t> ref_values_t;
            

            static const NodePersense f_addre_presence_c = fdef_1;
            //typedef std::tuple<presence_t, ref_reindex_hash_t, ref_stems_t> node_def_t;
            //node_def_t _data;

            presence_t presence;
            /**modification version of node*/
            version_t version;
            ref_reindex_hash_t reindexer;
            ref_stems_t stems;
            ref_values_t payload;
            /**Unique signature of node*/
            NodeUid uid;
            
            TrieNode()
                : version(0)
            {
            }
            template <class TSegmentTopology, class Atom>
            std::tuple<stem::StemCompareResult, dim_t, FarAddress> navigate_over(
                TSegmentTopology& topology, Atom& begin, Atom end) const
            {
                typedef ValueArrayManager<TSegmentTopology, payload_t> value_manager_t;

                assert(begin != end);
                auto key = static_cast<atom_t>(*begin);
                if (!presence.get(key)) //no such entry at all
                    return std::make_tuple(stem::StemCompareResult::unequals, 0, FarAddress());
                //else detect how long common part is
                ++begin; //first letter concidered in `presence`
                
                auto index = this->reindex(topology, key);
                
                if (!stems.is_null())
                {//let's cut prefix from stem container
                    stem::StemStore<TSegmentTopology> stem_manager(topology);
                    auto prefix_info = stem_manager.prefix_of(stems, index, begin, end);
                    std::get<1>(prefix_info)++;//because of ++begin need increase index

                    auto nav_type = tuple_ref<stem::StemCompareResult>(prefix_info);
                    if (nav_type == stem::StemCompareResult::stem_end)
                    { //stem ended, this mean either this is terminal or reference to other node
                        value_manager_t value_manager(topology);
                        auto v = value_manager.get(payload, index);
                        assert(v.has_child() || v.has_data()); //otherwise Trie is corrupted
                        return std::tuple_cat(prefix_info, std::make_tuple(v.get_child()));
                    }
                    //there since nav_type == string_end || nav_type == equals || nav_type == unequals
                    return std::tuple_cat(prefix_info, std::make_tuple(FarAddress(/*no ref-down*/)));
                }
                //no stems, just follow down
                assert(!payload.is_null());
                value_manager_t value_manager(topology);
                auto term = value_manager.get(payload, index);
                return std::make_tuple(
                    (begin == end) ? stem::StemCompareResult::equals : stem::StemCompareResult::stem_end,
                    dim_t{ 1 },
                    (begin == end) ? FarAddress() : term.child
                    );
            }
            
            
            /** 
            *  @param payload - may or not be used (if source string too long and should be placed to other page)
            */
            template <class TSegmentTopology, class Atom>
            void insert(TSegmentTopology& topology, Atom& begin, const Atom end, payload_t&& payload)
            {
                auto reindex_res = insert_stem(topology, begin, end);
                if (begin == end)
                { //source fully fit to stem
                    ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                    value_manager.put_data(this->payload, reindex_res, std::move(payload));
                }
            }
            template <class TSegmentTopology>
            void set_child(TSegmentTopology& topology, atom_t key, FarAddress address)
            {
                auto reindex_res = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                value_manager.put_child(payload, reindex_res, address);
            }
            template <class TSegmentTopology, class Payload>
            void set_value(TSegmentTopology& topology, atom_t key, Payload&& value)
            {
                auto reindex_res = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                value_manager.put_data(payload, reindex_res, std::forward<Payload>(value));
            }
            /**
            * Move entry from this specified by 'key' node that is started on 'at_index' to another one 
            * specified by 'target' address
            */
            template <class TSegmentTopology>
            void move_to(TSegmentTopology& topology, atom_t key, dim_t at_index, FarAddress target)
            {
                stem::StemStore<TSegmentTopology> stem_manager(topology);
                containers::PersistedHashTable<TSegmentTopology> hash_manager(topology);
                auto reindex_src = reindex(topology, key);
                auto target_node =
                    topology.segment_manager().wr_at<this_t>(target);
                //extract stem from current node
                /*std::tuple<const atom_t*, dim_t, stem::StemData& >*/auto stem_info = 
                    stem_manager.stem(stems, reindex_src);
                auto src_begin = stem::StemOfNode(key, at_index, std::get<0>(stem_info));
                auto src_end = stem::StemOfNode(key, std::get<1>(stem_info), std::get<0>(stem_info));
                auto length = std::get<1>(stem_info) - at_index;//how many bytes to copy
                //move this stem to target node
                auto reindex_target = target_node->insert_stem(topology,
                    src_begin,
                    src_end
                    );
                //copy data/address to target
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                auto& src_val = value_manager.getw(this->payload, reindex_src);
                auto& target_val = value_manager.getw(target_node->payload, reindex_target);
                target_val = std::move(src_val);
                //src_val.clear();//clear previous data/addr in source 
                //truncate stem in current node
                stem_manager.trunc_str(std::get<2>(stem_info), reindex_src,
                    std::get<1>(stem_info) - length);
            }
        private:
            /**
            * Take some part of string specified by [begin ,end) and place inside this node
            * @return origin index that matches to accomodated key (reindexed key)
            */
            template <class TSegmentTopology, class Atom>
            dim_t insert_stem(TSegmentTopology& topology, Atom& begin, Atom end)
            {
                stem::StemStore<TSegmentTopology> stem_manager(topology);
                
                auto key = static_cast<atom_t>(*begin);
                assert(!presence.get(key));
                ++begin; //first letter concidered in `presence`
                dim_t reindex_res = key;
                if (!stems.is_null())
                {
                    if (!this->reindexer.is_null())
                    {
                        containers::PersistedHashTable<TSegmentTopology> hash_mngr(topology);
                        auto p = hash_mngr.insert(this->reindexer, key);
                        while (!p.second) //may be on no capacity or dupplicate (that's impossible for normal flow)
                        {
                            assert(p.first == dim_nil_c);//only possible reason - capacity is over
                            grow(topology);
                            if (this->reindexer.is_null()) //might grow to 256 and became nil
                            {
                                p.first = key;
                                break;
                            }
                            else
                            {
                                p = hash_mngr.insert(this->reindexer, key);
                                //assert(p.second);//in new container 
                            }
                            
                        }
                        reindex_res = p.first;
                    }
                    stem_manager.accommodate(stems, (atom_t)reindex_res, begin, std::move(end));
                    
                }
                presence.set(key);

                return reindex_res;
            }
            template <class TSegmentTopology>
            atom_t reindex(TSegmentTopology& topology, atom_t key) const
            {
                if (this->reindexer.is_null()) //reindex may absent for 256 table
                    return key;
                containers::PersistedHashTable<TSegmentTopology> hash_mngr(topology);
                
                auto reindexed = hash_mngr.find(this->reindexer, key);
                assert(reindexed != hash_mngr.nil_c);

                return static_cast<atom_t>(reindexed);
            }
            
            template <class TSegmentTopology>
            void grow(TSegmentTopology& topology)
            {
                containers::PersistedHashTable<TSegmentTopology> hash_manager(topology);
                auto remap = hash_manager.grow(this->reindexer, presence.presence_begin(), presence.presence_end());
                
                
                //place to thread copying of data, while stems are copied in this thread
                
                stem::StemStore<TSegmentTopology> stem_manager(topology);
                //make new stem container
                auto stem_move_mngr = stem_manager.grow(this->stems, (dim_t)remap.new_capacity);
                //make new value-container
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                auto value_grow_res = value_manager.grow(this->payload, (fast_dim_t)remap.old_capacity, (fast_dim_t)remap.new_capacity);
                
                auto data_copy_future = std::async(std::launch::async, [this, &value_grow_res, &remap]() {
                    auto lambda = [&value_grow_res](dim_t old_idx, dim_t to_idx) {
                        value_grow_res.move(old_idx, to_idx);
                    };
                    this->copy_stuff(remap, lambda);
                });

                copy_stuff(remap, [&stem_move_mngr](dim_t old_idx, dim_t to_idx) {
                    stem_move_mngr.move(old_idx, to_idx);
                });
                stem_manager.destroy(this->stems);
                this->stems = stem_move_mngr.to_address();

                hash_manager.destroy(this->reindexer.address); //delete old
                this->reindexer = ref_reindex_hash_t(remap.new_address);
                
                data_copy_future.get();
                
                value_manager.destroy(this->payload);
                this->payload = value_grow_res.dest_addr();
            }
            /**Apply `move_callback` to each existing item to move from previous container to new container (like a value- or stem- containers) */
            template <class Remap, class MoveCallback>
            void copy_stuff(Remap& remap, MoveCallback& move_callback)
            {
                for (auto i = presence.presence_begin(); i != presence.presence_end(); ++i)
                {
                    auto old_idx = remap.old_map_function(static_cast<atom_t>(*i));
                    auto new_idx = remap.new_map_function(static_cast<atom_t>(*i));
                    move_callback(old_idx, new_idx);
                }
            }
        };
    } //ns:trie
}//ns:OP
    
#endif //_OP_TRIE_TRIENODE__H_
