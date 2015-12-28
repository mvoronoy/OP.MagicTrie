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
            static const dim_t max_stem_length_c = 256;
        };
        template <class Payload>
        struct TrieNode
        {
            typedef typename Payload payload_t;
            typedef Bitset<4, std::uint64_t> presence_t;
            typedef std::uint32_t version_t;
            typedef OP::trie::containers::HashTableData reindex_hash_t;
            typedef PersistedReference<reindex_hash_t> ref_reindex_hash_t;
            
            typedef OP::trie::stem::StemData stems_t;
            typedef PersistedReference<stems_t> ref_stems_t;

            typedef ValueArrayData<payload_t> values_t;
            typedef PersistedArray<values_t> ref_values_t;

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
            typedef std::tuple<bool, FarAddress> insert_result_t;
            /** @return <0> - false if duplicate was found, true mean need continue to research
            *
            */
            template <class TSegmentTopology, class Atom, class FSuffixInsert>
            insert_result_t insert(TSegmentTopology& topology, const Atom& begin, const Atom end, FSuffixInsert&& suffix_insert)
            {
                StemStore<TSegmentTopology> stem_manager(topology);
                PersistedHashTable<TSegmentTopology> hash_manager(topology);
                auto key = static_cast<atom_t>(*begin);
                if (presence.get(key))
                { //same atom already points somewhere
                    ++begin; //first letter concidered in `presence`
                    /**
                    *   |   src   |     stem    |
                    *  1    ""          ""         duplicate
                    *  2    ""           x         split stem on length of src, so terminal is for src and for x
                    *  3     x          ""         add x to page pointed by stem
                    *  4     x           y         create child with 2 entries: x, y (check if followed of y can be optimized)
                    */
                    auto index = this->reindex(key);
                    if (stems.address != SegmentDef::far_null_c)
                    {//let's cut prefix from stem container
                        auto stems = stems.ref(manager);
                        auto src_len = end - begin;
                        stem_manager.prefix_of(stems.address, index, begin, end);
                        auto rest_length = end - begin;
                        auto common_length = src_len - rest_length;
                        auto stem_rest = stems.stem_length[index] - common_length;
                        if (!stem_rest)
                        {
                            if (!rest_length)
                                return false; //case 1 - duplicate
                            if (!stem_rest)
                            {//case 3 - need continue insertion

                                return std::make_tuple(true, ;
                            }
                        }
                        if (rest_lengt > 0)
                        {//string not fully fit in stem container, need split
                                
                        }
                        auto after_len = end - begin;
                    }
                    //no stems
                }
                else //set new entry
                {
                    auto reindex_res = hash_manager.insert(this->reindex.address, key);
                    assert(reindex_res.second); //no-presence automatically mean no hash-entry
                    presence.set(key);
                    
                }
            }
        private:
            
            template <class TSegmentTopology>
            dim_t reindex(TSegmentTopology& topology, atom_t key)
            {
                if (this->reindexer.address == SegmentDef::far_null_c)
                    return key;
                auto reindexed = this->reindex.cref()->find(key);
                assert(reindexed != reindex_hash_t::nil_c);
                return reindexed;
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
                return _segment_manager->ro_at<TrieHeader>(_segment_address)->_root;
            }
            /**Total count of items in Trie*/
            std::uint64_t count()
            {
                return _segment_manager->ro_at<TrieHeader>(_segment_address)->_count;
            }
            /**Total number of nodes (pags) allocated in Trie*/
            std::uint64_t nodes_allocated()
            {
                return _segment_manager->ro_at<TrieHeader>(_segment_address)->_nodes_allocated;
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
            typedef Trie<TSegmentManager, Payload, initial_node_count> this_t;
            typedef TrieNode<Payload> node_t;
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
                auto node = _topology_ptr->segment_manager().ro_at<node_t>(r_addr);
                
                return navigator_t(_topology_ptr->slot<TrieResidence>().get_root_addr(), 
                    node->presence.first_set()/*may produce nil_c*/ 
                    );
            }
            navigator_t navigator_end() const
            {
                return navigator_t(FarAddress(SegmentDef::far_null_c), dim_t(~0u));
            }

            bool insert(const atom_t*& begin, const atom_t* end, Payload&& value, iterator * result = nullptr)
            {
                if (begin == end)
                    return false; //empty string cannot be inserted
                
                OP::vtm::TransactionGuard op_g(_topology_ptr->segment_manager().begin_transaction()); 
                auto root_addr = _topology_ptr->slot<TrieResidence>().get_root_addr();
                auto node = _topology_ptr->segment_manager().wr_at<node_t>(root_addr);
                node->

                node_ptr_t node = _root;
                while (begin != end)
                {
                    _node_manager.grant_state(*node, node_manager_t::findable_c);

                }
                const atom_t* prev_it = begin;
                unsigned pos = node->accommodate(begin, end);
                if (pos == ~0u)//no capacity to insert
                {
                    node = extend(node);
                    pos = node->accommodate(begin, end);
                }
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
                auto node =* new (_topology_ptr->segment_manager().wr_at<node_t>(node_pos)) node_t;
                // create hash-reindexer
                containers::PersistedHashTable<topology_t> hash_mngr(*_topology_ptr);
                node.reindexer.address = hash_mngr.create(containers::HashTableCapacity::_8);
                //create stem-container
                stem::StemStore<topology_t> stem_mngr(*_topology_ptr);
                node.stems.address = stem_mngr.create(
                    (dim_t)containers::HashTableCapacity::_8, 
                    TrieDef::max_stem_length_c);
                ValueArrayManager<topology_t, payload_t> vmanager(*_topology_ptr);
                node.payload.address = vmanager.create((dim_t)containers::HashTableCapacity::_8);

                auto &res = _topology_ptr->slot<TrieResidence>();
                res.increase_nodes_allocated(+1);
                return node_pos;
            }
        };
    }
}
#endif //_OP_TRIE_TRIE__H_
