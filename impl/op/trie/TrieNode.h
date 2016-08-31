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
        typedef std::uint32_t node_version_t;
        struct NodeUid
        {
            std::uint64_t uid;
        };
        inline bool operator == (const NodeUid& left, const NodeUid& right)
        {
            return left.uid == right.uid;
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
            node_version_t version;
            ref_reindex_hash_t reindexer;
            ref_stems_t stems;
            ref_values_t payload;
            /**Unique signature of node*/
            NodeUid uid;
            dim_t capacity;

            TrieNode()
                : version(0)
            {
            }
            struct nav_result_t
            {
                nav_result_t()
                    : compare_result{ stem::StemCompareResult::unequals }
                    //, stem_rest{ 0 }
                {}

                nav_result_t(stem::StemCompareResult a_compare_result, 
                    FarAddress a_child_node = FarAddress(), dim_t a_stem_rest = 0)
                    : compare_result{a_compare_result}
                    , child_node{ a_child_node }
                    //, stem_rest{ a_stem_rest }
                {}

                /** result of comare string with node */
                stem::StemCompareResult compare_result;
                /**address of further children to continue navigation*/
                FarAddress child_node;
                /** bytes that left in the node after string exhausted */
                //atom_string_t stem_rest;
            };

            template <class TSegmentTopology, class Atom, class Iterator>
            nav_result_t sync_tail(
                TSegmentTopology& topology, Atom& begin, Atom end, Iterator& track_back) const
            {
                auto& back = track_back.back();
                atom_t key = back.key();
                assert(presence.get(key));
                nav_result_t retval;
                atom_t index = this->reindex(topology, key);
                if (!stems.is_null())
                { //there is a stem
                    stem::StemStore<TSegmentTopology> stem_manager(topology);
                    stem_manager.stem(stems, key, [&](const atom_t *f_str, const atom_t *f_str_end, const StemData& stem_header) {
                        for (; f_str != f_str_end && begin != end; ++back.deep, ++begin, ++f_str)
                        {
                            if (*f_str != *begin)
                            { //difference in sequence mean that stem should be splitted
                                retval.compare_result = stem::StemCompareResult::unequals;
                                return;//stop at: tuple(StemCompareResult::unequals, i);
                            }
                        }
                        //correct result type as one of string_end, equals, stem_end
                        retval.compare_result = f_str != f_str_end
                            ? (StemCompareResult::string_end)
                            : (begin == end ? StemCompareResult::equals : StemCompareResult::stem_end);
                    });
                }
                else 
                { 
                    assert(back.deep() == 1);
                }
            }
            /**
            * @para track_back - iterator that is populated at exit. 
            *   @return \li get<0> - compare result;
            *   \li get<1> length of string overlapped part;
            *   \li get<2> address of further children to continue navigation
            */
            template <class TSegmentTopology, class Atom, class Iterator>
            nav_result_t navigate_over(
                TSegmentTopology& topology, Atom& begin, Atom end, FarAddress this_node_addr, Iterator& track_back ) const
            {
                typedef ValueArrayManager<TSegmentTopology, payload_t> value_manager_t;

                assert(begin != end);
                auto key = static_cast<atom_t>(*begin);
                nav_result_t retval;
                if (!presence.get(key)) //no such entry at all
                {
                    retval.compare_result = stem::StemCompareResult::no_entry;
                    return retval;
                }
                //else detect how long common part is
                ++begin; //first letter concidered in `presence`
                auto origin_begin = begin;

                atom_t index = this->reindex(topology, key);
                value_manager_t value_manager(topology);
                auto& term = value_manager.view(payload, (dim_t)capacity)[index];
                TriePosition pos(this_node_addr, 
                    this->uid, key, 1/*dedicated for presence*/, this->version);

                retval.compare_result = stem::StemCompareResult::equals;
                if (!stems.is_null())
                { //there is a stem
                    stem::StemStore<TSegmentTopology> stem_manager(topology);
                    //if (begin != end)//string is not over 
                    {//let's cut prefix from stem container
                        dim_t deep = 0;
                        std::tie(retval.compare_result, deep) = stem_manager.prefix_of(stems, index, begin, end/*, &retval.stem_rest*/);
                        pos._deep += deep;

                        if (retval.compare_result == stem::StemCompareResult::stem_end)
                        { //stem ended, this mean either this is terminal or reference to other node
                            assert(term.has_child() || term.has_data()); //otherwise Trie is corrupted
                            retval.child_node = term.get_child();
                            pos._terminality = (Terminality)term.presence;
                        }
                        else if (retval.compare_result == stem::StemCompareResult::equals)
                        {
                            if (!term.has_data())
                            {   //case when string is over exactly on this node
                                assert(term.has_child());
                                retval.compare_result = stem::StemCompareResult::string_end;
                            }
                            retval.child_node = term.get_child();
                            pos._terminality = (Terminality)(term.presence);
                        } //terminality there = term_no
                        //populate iterator 
                    }
                }// end checking stem container
                else 
                {//no stems, just follow down
                    assert(!payload.is_null());
                    if (begin != end)
                    {
                        retval.compare_result = stem::StemCompareResult::stem_end;
                        retval.child_node = term.get_child();
                    }
                }
                track_back.emplace(std::move(pos), origin_begin, begin);
                return retval;
            }
            
            
            /** 
            *  @param payload_factory - may or not be used (if source string too long and should be placed to other page)
            *  @return true when end of iteration was reached and value assigned, false mean no value inserted
            */
            template <class TSegmentTopology, class Atom, class ProducePayload>
            bool insert(TSegmentTopology& topology, Atom& begin, const Atom end, ProducePayload& payload_factory)
            {
                auto reindex_res = insert_stem(topology, begin, end);
                if (begin == end)
                { //source fully fit to stem
                    ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                    value_manager.accessor(this->payload, this->capacity)[reindex_res].set_data(std::move(payload_factory()));
                    return true;
                }
                return false;
            }
            /**
            * @return true if entire node should be deleted
            */
            template <class TSegmentTopology>
            bool erase(TSegmentTopology& topology, atom_t key, bool erase_data)
            {
                containers::PersistedHashTable<TSegmentTopology> hash_mngr(topology);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                //no need to operate by stem
                dim_t reindexed = key;
                auto has_reindexer = !this->reindexer.is_null();
                if (has_reindexer) //reindex may absent for 256 table
                {
                    auto table_head = hash_mngr.table_head(this->reindexer);
                    reindexed = hash_mngr.find(table_head, key);
                    assert(reindexed != hash_mngr.nil_c);
                }
                assert(presence.get(key));
                if (erase_data)
                {
                    value_manager.accessor(payload, capacity)[reindexed].clear_data();
                }
                if (value_manager.accessor(payload, capacity)[reindexed].has_something())
                { //when child presented need keep sequence in this node
                    return false;
                }
                //no child, so wipe content
                if (has_reindexer)
                {
                    hash_mngr.erase(this->reindexer, static_cast<atom_t>(reindexed));
                }
                presence.clear(key);
                //@! think to reduce space of hashtable
                return presence.first_set() == presence_t::nil_c; //erase entire node if no more entries
            }
            
            template <class TSegmentTopology>
            void set_child(TSegmentTopology& topology, atom_t key, FarAddress address)
            {
                atom_t reindexed = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                value_manager.accessor(payload, capacity)[ reindexed ].set_child(address);
                version++;
            }
            /**Get child address if present, otherwise return null-pos*/
            template <class TSegmentTopology>
            PersistedReference<this_t> get_child(TSegmentTopology& topology, atom_t key) const
            {
                atom_t reindexed = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                return PersistedReference<this_t> (
                    value_manager.view(payload, capacity)[reindexed].get_child() );
            }
            /**Get child address if present, otherwise return null-pos*/
            template <class TSegmentTopology>
            payload_t get_value(TSegmentTopology& topology, atom_t key) const
            {
                atom_t reindexed = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                auto& vad = value_manager.view(payload, capacity)[reindexed];
                if (!vad.has_data())
                    throw std::invalid_argument("key doesn't contain data");
                return vad.data;
            }
            template <class TSegmentTopology, class Payload>
            void set_value(TSegmentTopology& topology, atom_t key, Payload&& value)
            {
                atom_t reindexed = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                value_manager.accessor(payload, capacity)[ reindexed ].set_data(std::move(value));
                version++;
            }
            template <class TSegmentTopology>
            std::pair<bool, bool> get_presence(TSegmentTopology& topology, atom_t key) const
            {
                atom_t reindexed = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                auto& vad = value_manager.view(payload, capacity)[reindexed];
                return std::make_pair(vad.has_child(), vad.has_data());
            }
            /**@return first position where child or value exists, may return dim_nil_c if node empty*/
            inline nullable_atom_t first() const
            {
                return make_nullable( this->presence.first_set() );
            }
            /**@return last position where child or value exists, may return dim_nil_c if node empty*/
            inline nullable_atom_t last() const
            {
                return make_nullable(this->presence.last_set());
            }
            /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
            nullable_atom_t next(atom_t previous) const
            {
                return make_nullable( this->presence.next_set(previous) );
            }
            /**@return next or the same position where child or value exists, may return dim_nil_c if no more entries*/
            nullable_atom_t next_or_this(atom_t previous) const
            {
                return make_nullable(this->presence.next_set_or_this(previous));
            }
            /**
            * Move entry from this specified by 'key' node that is started on 'in_stem_pos' to another one 
            * specified by 'target' address
            */
            template <class TSegmentTopology>
            void move_to(TSegmentTopology& topology, atom_t key, dim_t in_stem_pos, FarAddress target)
            {
                assert(in_stem_pos > 0); //in_stem_pos - cannot be == 0 because 0 dedicated for hash-table (reindexer), stem always coded from 1
                --in_stem_pos;
                stem::StemStore<TSegmentTopology> stem_manager(topology);
                containers::PersistedHashTable<TSegmentTopology> hash_manager(topology);
                atom_t ridx = reindex(topology, key);
                auto target_node =
                    topology.segment_manager().wr_at<this_t>(target);
                dim_t reindex_target;
                //extract stem from current node
                stem_manager.stemw(stems, ridx, [&](const atom_t* src_begin, const atom_t* src_end, stem::StemData& stem_header) -> void{
                    assert(in_stem_pos <= (src_end - src_begin));
                    auto start = src_begin + in_stem_pos;
                    reindex_target = target_node->insert_stem(topology, start, src_end);
                    //truncate stem in current node
                    stem_manager.trunc_str(stem_header, ridx, in_stem_pos );
                });

                //copy data/address to target
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                auto& src_val = value_manager.accessor(this->payload, this->capacity) [ridx];
                auto& target_val = value_manager.accessor(target_node->payload, target_node->capacity)[reindex_target];
                target_val = std::move(src_val);// move clears previous data/addr in source 
                version++;
            }
            /**
            *   Taken a byte key, return index where corresponding key should reside for stem_manager and value_manager
            *   @return reindexed key (value is in range [_8, _256) ). 
            */
            template <class TSegmentTopology>
            atom_t reindex(TSegmentTopology& topology, atom_t key) const
            {
                if (this->reindexer.is_null()) //reindex may absent for 256 table
                    return key;
                containers::PersistedHashTable<TSegmentTopology> hash_mngr(topology);
                auto table_head = hash_mngr.table_head(this->reindexer);
                auto reindexed = hash_mngr.find(table_head, key);
                assert(reindexed != hash_mngr.nil_c);

                return static_cast<atom_t>(reindexed);
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
                version++;
                return reindex_res;
            }
            
            
            template <class TSegmentTopology>
            void grow(TSegmentTopology& topology)
            {
                containers::PersistedHashTable<TSegmentTopology> hash_manager(topology);
                auto remap = hash_manager.grow(this->reindexer, presence.presence_begin(), presence.presence_end());
                capacity = (dim_t)remap.new_capacity;
                
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

                data_copy_future.get();

                hash_manager.destroy(this->reindexer); //delete old
                this->reindexer = ref_reindex_hash_t(remap.new_address);
                
                
                value_manager.destroy(this->payload);
                this->payload = value_grow_res.dest_addr();
                version++;
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
