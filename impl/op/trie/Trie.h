#ifndef _OP_TRIE_TRIE__H_
#define _OP_TRIE_TRIE__H_

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <op/trie/Containers.h>
#include <op/trie/FixedSizeMemoryManager.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Bitset.h>
#include <op/trie/HashTable.h>
#include <op/trie/StemContainer.h>
#include <op/trie/MemoryManager.h>
#include <op/trie/TypeHelper.h>
#include <op/trie/ValueArray.h>
#include <op/trie/StemContainer.h>

namespace OP
{
    namespace trie
    {
        template <class TNode, class TSegmentManager>
        struct TrieNavigator
        {
            TrieNavigator(FarAddress node_addr, dim_t offset)
                : _node_addr(node_addr)
                , _offset(offset)
            {}
            bool operator == (const TrieNavigator& other) const
            {
                return _offset == other._offset && (_offset == dim_t(-1) || _node_addr == other._node_addr );
            }
            
        private:
            FarAddress _node_addr;
            dim_t _offset;
        };

        
        /**Constant definition for trie*/
        struct TrieDef
        {
            /**Maximal length of stem*/
            static const dim_t max_stem_length_c = 255;
        };
        template <class Payload>
        struct TrieNode
        {
            typedef typename Payload payload_t;
            typedef TrieNode<payload_t> this_t;

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
            version_t version;
            ref_reindex_hash_t reindexer;
            ref_stems_t stems;
            ref_values_t payload;

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
                    auto prefix_info = stem_manager.prefix_of(stems.address, index, begin, end);
                    std::get<1>(prefix_info)++;//because of ++begin need increase index

                    stem::StemCompareResult nav_type = std::get<0>(prefix_info);
                    if (nav_type == stem::StemCompareResult::stem_end)
                    { //stem ended, this mean either this is terminal or reference to other node
                        value_manager_t value_manager(topology);
                        auto v = value_manager.get(payload.address, index);
                        return std::tuple_cat(prefix_info, std::make_tuple(v.get_child()));
                    }
                    //there since nav_type == string_end || nav_type == equals || nav_type == unequals
                    return std::tuple_cat(prefix_info, std::make_tuple(FarAddress(/*no ref-down*/)));
                }
                //no stems, just follow down
                assert(!payload.is_null());
                value_manager_t value_manager(topology);
                auto term = value_manager.get(payload.address, index);
                return std::make_tuple(
                    (begin == end) ? stem::StemCompareResult::equals : stem::StemCompareResult::stem_end,
                    1,
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
                    value_manager.put_data(this->payload.address, reindex_res, std::move(payload));
                }
            }
            template <class TSegmentTopology>
            void set_child(TSegmentTopology& topology, atom_t key, FarAddress address)
            {
                auto reindex_res = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                value_manager.put_child(payload.address, reindex_res, address);
            }
            template <class TSegmentTopology, class Payload>
            void set_value(TSegmentTopology& topology, atom_t key, Payload&& value)
            {
                auto reindex_res = reindex(topology, key);
                ValueArrayManager<TSegmentTopology, payload_t> value_manager(topology);
                value_manager.put_data(payload.address, reindex_res, std::forward<Payload>(value));
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
                std::tuple<const atom_t*, dim_t&, stem::StemData& > stem_info = 
                    stem_manager.stem(stems.address, reindex_src);
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
                auto& src_val = value_manager.getw(this->payload.address, reindex_src);
                auto& target_val = value_manager.getw(target_node->payload.address, reindex_target);
                target_val = std::move(src_val);
                //src_val.clear();//clear previous data/addr in source 
                //truncate stem in current node
                stem_manager.trunc_str(std::get<2>(stem_info), reindex_src,
                    std::get<1>(stem_info) - length);
            }
        private:
            /**return origin index that matches to accomodated key (reindexed key)*/
            template <class TSegmentTopology, class Atom>
            dim_t insert_stem(TSegmentTopology& topology, Atom& begin, Atom end)
            {
                stem::StemStore<TSegmentTopology> stem_manager(topology);
                
                auto key = static_cast<atom_t>(*begin);
                ++begin; //first letter concidered in `presence`
                assert(!presence.get(key));
                presence.set(key);
                dim_t reindex_res = key;
                if (this->reindexer.is_null())
                {
                    containers::PersistedHashTable<TSegmentTopology> hash_mngr(topology);
                    auto p = hash_mngr.insert(this->reindexer.address, key);
                    if (!p.second) //may be on no capacity or dupplicate (that's impossible for normal flow)
                    {
                        assert(p.first == ~dim_t(0));//only possible reason - capacity is over
                        grow(topology);
                    }
                }
                
                auto reindex_res = reindex(topology, key);
                stem_manager.accommodate(stems.address, reindex_res, begin, std::move(end));
                return reindex_res;
            }
            template <class TSegmentTopology>
            atom_t reindex(TSegmentTopology& topology, atom_t key) const
            {
                if (this->reindexer.address == SegmentDef::far_null_c)
                    return key;
                containers::PersistedHashTable<TSegmentTopology> hash_mngr(topology);
                
                auto reindexed = hash_mngr.find(this->reindexer.address, key);
                assert(reindexed != hash_mngr.nil_c);

                return static_cast<atom_t>(reindexed);
            }
            /**Helper class to keep track of states durin grow operation*/
            template <class TSegmentTopology>
            struct GrowTrackState
            {
                GrowTrackState(TSegmentTopology& toplogy, ref_stems_t& stems_ref, ref_values_t& values_ref )
                    : _stem_manager(topology)
                    , _value_manager(topology)
                    , _stems_ref(stems_ref)
                    , _values_ref(values_ref)
                {}

                /**Create new containers for values and stems*/
                void pre_grow(containers::HashTableCapacity new_capacity)
                {
                    _new_stems = std::move(_stem_manager.grow(_stems_ref, (dim_t)new_capacity);
                    _new_vad = _value_manager.create((dim_t)new_capacity);
                }
                void copy_data(atom_t from, dim_t to)
                {
                    tuple_ref<StemData*>(_new_stems)
                }
                stem::StemStore<TSegmentTopology> _stem_manager;
                ValueArrayManager<TSegmentTopology, payload_t> _value_manager;
                ref_stems_t& _stems_ref;
                std::tuple<FarAddress, StemData*> _new_stems;
                FarAddress _new_vad;
                ref_values_t& _values_ref;
            };
            template <class TSegmentTopology>
            void grow(TSegmentTopology& toplogy)
            {
                stem::StemStore<TSegmentTopology> stem_manager(topology);
                containers::PersistedHashTable<TSegmentTopology> hash_manager(topology);

                hash_manager.grow(this->reindexer,
                    [](containers::HashTableCapacity new_capacity) {
                    //
                }
                auto new_capacity = details::grow_size((HashTableCapacity)prev_tbl_head->capacity);
                
            }
        };

        template <class Payload>
        struct SubTrie
        {
            /**start lexicographical ascending iteration over trie content. Following is a sequence of iteration:
            *   - a
            *   - aaaaaaaaaa
            *   - abcdef
            *   - b
            *   - ...
            */
                        
            virtual std::unique_ptr<SubTrie<Payload> > subtree(const atom_t*& begin, const atom_t* end) const = 0;
            
        };
        /**
        *   Small slot to keep arbitrary Trie information in 0 segment
        */
        struct TrieResidence : public Slot
        {
            template <class TSegmentManager, class Payload, std::uint32_t initial_node_count>
            friend struct Trie;
            /**Keep address of root node of Trie*/
            FarAddress get_root_addr()
            {
                auto ro_block = get_header_block();
                return ro_block.at<TrieHeader>(0)->_root;
            }
            /**Total count of items in Trie*/
            std::uint64_t count()
            {
                auto ro_block = get_header_block();
                return ro_block.at<TrieHeader>(0)->_count;
            }
            /**Total number of nodes (pags) allocated in Trie*/
            std::uint64_t nodes_allocated()
            {
                auto ro_block = get_header_block();
                return ro_block.at<TrieHeader>(0)->_nodes_allocated;
            }
        private:
            struct TrieHeader
            {
                TrieHeader()
                    : _root{}
                    , _count(0)
                    , _nodes_allocated(0)
                {}
                /**Where root resides*/
                FarAddress _root;
                /**Total count of terminal entries*/
                std::uint64_t _count;
                /**Number of nodes (pages) allocated*/
                std::uint64_t _nodes_allocated;
            };
            ReadonlyMemoryRange get_header_block() const
            {
                return _segment_manager->readonly_block(_segment_address, sizeof(TrieHeader));
            }
            FarAddress _segment_address;
            SegmentManager* _segment_manager;
        protected:
                        /**
            *   Set new root node for Trie
            *   @throws TransactionIsNotStarted if method called outside of transaction scope
            */
            TrieResidence& set_root_addr(FarAddress new_root)
            {
                _segment_manager->wr_at<TrieHeader>(_segment_address)->_root = new_root;
                return *this;
            }
            /**Increase/decrease total count of items in Trie. 
            *   @param delta positive/negative numeric value to modify counter
            *   @throws TransactionIsNotStarted if method called outside of transaction scope
            */
            TrieResidence& increase_count(int delta)
            {
                _segment_manager->wr_at<TrieHeader>(_segment_address)->_count += delta;
                return *this;
            }

            /**Increase/decrease total number of nodes in Trie. 
            *   @param delta positive/negative numeric value to modify counter
            *   @throws TransactionIsNotStarted if method called outside of transaction scope
            */
            TrieResidence& increase_nodes_allocated(int delta)
            {
                _segment_manager->wr_at<TrieHeader>(_segment_address)->_nodes_allocated += delta;
                return *this;
            }
            //
            //  Overrides
            //
            /**Slot resides in zero-segment only*/
            bool has_residence(segment_idx_t segment_idx, SegmentManager& manager) const override
            {
                return segment_idx == 0; //true only for 0
            }
            /**Reserve enough to keep TrieHeader*/
            segment_pos_t byte_size(FarAddress segment_address, SegmentManager& manager) const override
            {
                assert(segment_address.segment == 0);
                return memory_requirement<TrieHeader>::requirement;
            }
            void on_new_segment(FarAddress segment_address, SegmentManager& manager) override
            {
                assert(segment_address.segment == 0);
                _segment_address = segment_address;
                _segment_manager = &manager;
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                *manager.wr_at<TrieHeader>(segment_address, OP::trie::WritableBlockHint::new_c)
                    = TrieHeader(); //init with null
                op_g.commit();
            }
            void open(FarAddress segment_address, SegmentManager& manager) override
            {
                assert(segment_address.segment == 0);
                _segment_manager = &manager;
                _segment_address = segment_address;
            }
            void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
            {
                
            }
        };
        template <class TSegmentManager, class Payload, std::uint32_t initial_node_count = 1024>
        struct Trie
        {
        public:
            typedef int iterator;
            typedef Payload payload_t;
            typedef Trie<TSegmentManager, payload_t, initial_node_count> this_t;
            typedef TrieNode<payload_t> node_t;
            typedef TrieNavigator<node_t, TSegmentManager> navigator_t;

            virtual ~Trie()
            {
            }
            
            static std::shared_ptr<Trie> create_new(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                //create new file
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                //make root for trie
                OP::vtm::TransactionGuard op_g(segment_manager->begin_transaction()); //invoke begin/end write-op
                r->_topology_ptr->slot<TrieResidence>().set_root_addr(r->new_node());
                
                op_g.commit();
                return r;
            }
            static std::shared_ptr<Trie> open(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                return r;
            }
            /**Total number of items*/
            std::uint64_t size()
            {
                return _topology_ptr->slot<TrieResidence>().count();
            }
            /**Number of allocated nodes*/
            std::uint64_t nodes_count()
            {
                return _topology_ptr->slot<TrieResidence>().nodes_allocated();
            }

            navigator_t navigator_begin()
            {
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction()); //place all RO operations to atomic scope
                auto r_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                auto root_block = _topology_ptr->segment_manager().readonly_block(r_addr, 
                    memory_requirement<node_t>::requirement);
                auto node = root_block.at<node_t>(0);
                
                return navigator_t(_topology_ptr->slot<TrieResidence>().get_root_addr(), 
                    node->presence.first_set()/*may produce nil_c*/ 
                    );
            }
            navigator_t navigator_end() const
            {
                return navigator_t(FarAddress(SegmentDef::far_null_c), dim_t(~0u));
            }
            template <class Atom>
            bool insert(Atom begin, Atom end, Payload&& value, iterator * result = nullptr)
            {
                if (begin == end)
                    return false; //empty string cannot be inserted
                
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction(), true); 
                auto node_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                for (;;)
                {
                    auto node =
                        _topology_ptr->segment_manager().readonly_access<node_t>(node_addr);
                    
                    atom_t key = *begin;
                    auto nav_res = node->navigate_over(*_topology_ptr, begin, end);
                    switch (tuple_ref<stem::StemCompareResult>(nav_res))
                    {
                    case stem::StemCompareResult::equals:
                        op_g.rollback();
                        return false; //dupplicate found
                    case stem::StemCompareResult::stem_end: //stem is over, just follow downstair to next node
                    {
                        auto next_addr = tuple_ref<FarAddress>(nav_res);
                        if (next_addr.address == SegmentDef::far_null_c)
                        {  //no way down
                            next_addr = new_node();
                            auto wr_node_block =
                                _topology_ptr->segment_manager().upgrade_to_writable_block(node);
                            auto wr_node = wr_node_block.at<node_t>(0);
                            wr_node->set_child(*_topology_ptr, key, next_addr);
                        }
                        node_addr = next_addr;
                        break;
                    }
                    case stem::StemCompareResult::string_end:
                    {
                        //terminal is not there, otherwise it would be StemCompareResult::equals
                        auto wr_node = tuple_ref<node_t*>(diversificate(node, key, tuple_ref<dim_t>(nav_res)));
                        wr_node->set_value(*_topology_ptr, key, std::forward<Payload>(value));
                        op_g.commit();
                        return true;
                    }
                    case stem::StemCompareResult::unequals:
                    {
                        auto in_stem_pos = tuple_ref<dim_t>(nav_res);
                        if (in_stem_pos == 0)//no such entry at all
                        {
                            auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                            wr_node->insert(*_topology_ptr, begin, end, std::forward<payload_t>(value));
                            if (begin == end) //fully fit to this node
                            {
                                op_g.commit();
                                return true;
                            }
                            //some suffix have to be accomodated yet
                            node_addr = new_node();
                            wr_node->set_child(*_topology_ptr, key, node_addr);
                        }
                        else //entry in this node should be splitted on 2
                            node_addr = tuple_ref<FarAddress>(
                                diversificate(node, key, in_stem_pos));
                        //continue in another node
                        break;
                    }
                    default:
                        assert(false);
                    }
                } //for(;;)
            }
        private:
            typedef FixedSizeMemoryManager<node_t, initial_node_count> node_manager_t;
            typedef SegmentTopology<
                TrieResidence, 
                node_manager_t, 
                MemoryManager/*Memory manager must go last*/> topology_t;
            std::unique_ptr<topology_t> _topology_ptr;
        private:
            Trie(std::shared_ptr<TSegmentManager>& segments)
            {
                _topology_ptr = std::make_unique<topology_t>(segments);
            }
            /** Create new node with default requirements. 
            * It is assumed that exists outer transaction scope.
            */
            FarAddress new_node()
            {
                auto node_pos = _topology_ptr->slot<node_manager_t>().allocate();
                auto node_block = _topology_ptr->segment_manager().writable_block(
                    node_pos, memory_requirement<node_t>::requirement);
                auto node = new (node_block.pos()) node_t;
                
                // create hash-reindexer
                containers::PersistedHashTable<topology_t> hash_mngr(*_topology_ptr);
                node->reindexer.address = hash_mngr.create(containers::HashTableCapacity::_8);
                //create stem-container
                stem::StemStore<topology_t> stem_mngr(*_topology_ptr);
                node->stems.address = stem_mngr.create(
                    (dim_t)containers::HashTableCapacity::_8, 
                    TrieDef::max_stem_length_c);
                ValueArrayManager<topology_t, payload_t> vmanager(*_topology_ptr);
                node->payload.address = vmanager.create((dim_t)containers::HashTableCapacity::_8);

                auto &res = _topology_ptr->slot<TrieResidence>();
                res.increase_nodes_allocated(+1);
                return node_pos;
            }
            /**Return pair of:
            * \li current node in write-mode (matched to `node_addr`);
            * \li address of new node where the tail was placed.
            */
            std::tuple<node_t*, FarAddress> diversificate(ReadonlyAccess<node_t>& node, atom_t key, dim_t in_stem_pos)
            {
                auto wr_node = _topology_ptr->segment_manager().upgrade_to_writable_block(node).at<node_t>(0);
                //create new node to place result
                FarAddress new_node_addr = new_node();
                wr_node->move_to(*_topology_ptr, key, in_stem_pos, new_node_addr);
                wr_node->set_child(*_topology_ptr, key, new_node_addr);
                return std::make_tuple(wr_node, new_node_addr);
            }
        };
    }
}
#endif //_OP_TRIE_TRIE__H_
