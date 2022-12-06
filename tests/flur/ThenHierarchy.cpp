#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <numeric>
#include <vector>
#include <string>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

namespace
{
    inline auto child_str_no_mor3(const std::string& t)
    {
        std::vector<std::string> res;
        if (t.size() && std::isalpha(t[0])) //render child for alpha only
        {
            if (t.size() < (3 * 2 + 1))
            {
                for (auto i = 0; i < 3; ++i)
                    res.emplace_back(t + "." + std::to_string(i));
            }
        }
        return src::of_container(std::move(res));
    }
    void test_HierarchyDeepFirstSimple(OP::utest::TestRuntime& rt)
    {
        std::vector<std::string> src{ "0", "a", "1" };
        auto simpl_hier_traverse = src::of_container(src)
            >> then::hierarchy_deep_first([](const std::string & t)
            {
                std::vector<std::string> res;
                if (t == "a") //render child for alpha only and 1 lev only
                {
                    res.emplace_back(t + ".0");
                }
                return src::of_container(std::move(res));
            });
        //for (auto x : simpl_hier_traverse)
        //    rt.debug() << x << ", ";
        rt.assert_that<eq_sets>(simpl_hier_traverse, std::vector<std::string>{ "0", "a", "a.0", "1" });
    }

    void test_HierarchyDeepFirst(OP::utest::TestRuntime& rt)
    {
        std::vector<std::string> src{ "0", "a", "1", "b", "c" };
        auto ll = src::of_container(src) >> then::hierarchy_deep_first(child_str_no_mor3);
        //for (auto x : ll)
        //    rt.debug() << x << ", ";
        rt.assert_that<eq_sets>(ll, std::vector<std::string>{
               "0", //no child
               "a", 
                "a.0", 
                    "a.0.0", 
                        "a.0.0.0", "a.0.0.1", "a.0.0.2", 
                    "a.0.1", 
                        "a.0.1.0", "a.0.1.1", "a.0.1.2", 
                    "a.0.2", 
                        "a.0.2.0", "a.0.2.1", "a.0.2.2", 
                "a.1", 
                    "a.1.0", 
                        "a.1.0.0", "a.1.0.1", "a.1.0.2", 
                    "a.1.1", 
                        "a.1.1.0", "a.1.1.1", "a.1.1.2", 
                    "a.1.2", 
                        "a.1.2.0", "a.1.2.1", "a.1.2.2", 
                "a.2", 
                    "a.2.0", 
                        "a.2.0.0", "a.2.0.1", "a.2.0.2", 
                     "a.2.1", 
                        "a.2.1.0", "a.2.1.1", "a.2.1.2", 
                     "a.2.2", 
                        "a.2.2.0", "a.2.2.1", "a.2.2.2", 
               "1", //no child
               "b", 
                "b.0", 
                    "b.0.0", 
                        "b.0.0.0", "b.0.0.1", "b.0.0.2", 
                    "b.0.1", 
                        "b.0.1.0", "b.0.1.1", "b.0.1.2", 
                     "b.0.2", 
                        "b.0.2.0", "b.0.2.1", "b.0.2.2", 
                "b.1", 
                    "b.1.0", 
                        "b.1.0.0", "b.1.0.1", "b.1.0.2", 
                    "b.1.1", 
                        "b.1.1.0", "b.1.1.1", "b.1.1.2", 
                    "b.1.2", 
                        "b.1.2.0", "b.1.2.1", "b.1.2.2", 
                "b.2", 
                    "b.2.0", 
                        "b.2.0.0", "b.2.0.1", "b.2.0.2", 
                    "b.2.1", 
                        "b.2.1.0", "b.2.1.1", "b.2.1.2", 
                    "b.2.2", 
                        "b.2.2.0", "b.2.2.1", "b.2.2.2", 
               "c", 
                "c.0", 
                    "c.0.0", 
                        "c.0.0.0", "c.0.0.1", "c.0.0.2", 
                    "c.0.1", 
                        "c.0.1.0", "c.0.1.1", "c.0.1.2", 
                    "c.0.2", 
                        "c.0.2.0", "c.0.2.1", "c.0.2.2", 
                "c.1", 
                    "c.1.0", 
                        "c.1.0.0", "c.1.0.1", "c.1.0.2", 
                    "c.1.1", 
                        "c.1.1.0", "c.1.1.1", "c.1.1.2", 
                    "c.1.2", 
                        "c.1.2.0", "c.1.2.1", "c.1.2.2", 
                "c.2", 
                    "c.2.0", 
                        "c.2.0.0", "c.2.0.1", "c.2.0.2", 
                    "c.2.1", 
                        "c.2.1.0", "c.2.1.1", "c.2.1.2", 
                    "c.2.2", 
                        "c.2.2.0", "c.2.2.1", "c.2.2.2"
            });
        auto empty_test = src::null<const std::string&>() 
            >> then::hierarchy_deep_first(child_str_no_mor3);
        rt.assert_that<eq_sets>(empty_test, std::vector<std::string>{});
        rt.info() << "Child resolve renders empty set...\n";
        rt.assert_that<eq_sets>(
            src::of_container(src)
            >> then::hierarchy_deep_first(
                [](const std::string& s) {return src::null<const std::string&>(); }),
            src,
            OP_CODE_DETAILS("Must coincedence with source set")
            );
    }

    void test_HierarchyBreadthFirst(OP::utest::TestRuntime& rt)
    {
        std::vector<std::string> src{ "0", "a", "1", "b", "c" };
        auto ll = src::of_container(src) >> then::hierarchy_breadth_first(child_str_no_mor3);
        //for (auto x : ll)
        //    rt.debug() << x << ", ";
        rt.assert_that<eq_sets>(ll, std::vector<std::string>{
            "0", "a", "1", "b", "c", 
            "a.0", "a.1", "a.2", 
            "b.0", "b.1", "b.2", 
            "c.0", "c.1", "c.2", 
            "a.0.0", "a.0.1", "a.0.2", 
            "a.1.0", "a.1.1", "a.1.2", 
            "a.2.0", "a.2.1", "a.2.2", 
            "b.0.0", "b.0.1", "b.0.2", 
            "b.1.0", "b.1.1", "b.1.2", 
            "b.2.0", "b.2.1", "b.2.2", 
            "c.0.0", "c.0.1", "c.0.2", 
            "c.1.0", "c.1.1", "c.1.2", 
            "c.2.0", "c.2.1", "c.2.2", 
            "a.0.0.0", "a.0.0.1", "a.0.0.2", "a.0.1.0", "a.0.1.1", "a.0.1.2", "a.0.2.0", "a.0.2.1", "a.0.2.2", 
            "a.1.0.0", "a.1.0.1", "a.1.0.2", "a.1.1.0", "a.1.1.1", "a.1.1.2", "a.1.2.0", "a.1.2.1", "a.1.2.2", 
            "a.2.0.0", "a.2.0.1", "a.2.0.2", "a.2.1.0", "a.2.1.1", "a.2.1.2", "a.2.2.0", "a.2.2.1", "a.2.2.2", 
            "b.0.0.0", "b.0.0.1", "b.0.0.2", "b.0.1.0", "b.0.1.1", "b.0.1.2", "b.0.2.0", "b.0.2.1", "b.0.2.2", 
            "b.1.0.0", "b.1.0.1", "b.1.0.2", "b.1.1.0", "b.1.1.1", "b.1.1.2", "b.1.2.0", "b.1.2.1", "b.1.2.2", 
            "b.2.0.0", "b.2.0.1", "b.2.0.2", "b.2.1.0", "b.2.1.1", "b.2.1.2", "b.2.2.0", "b.2.2.1", "b.2.2.2", 
            "c.0.0.0", "c.0.0.1", "c.0.0.2", "c.0.1.0", "c.0.1.1", "c.0.1.2", "c.0.2.0", "c.0.2.1", "c.0.2.2", 
            "c.1.0.0", "c.1.0.1", "c.1.0.2", "c.1.1.0", "c.1.1.1", "c.1.1.2", "c.1.2.0", "c.1.2.1", "c.1.2.2", 
            "c.2.0.0", "c.2.0.1", "c.2.0.2", "c.2.1.0", "c.2.1.1", "c.2.1.2", "c.2.2.0", "c.2.2.1", "c.2.2.2"});

        auto empty_test = src::null<std::string>()
            >> then::hierarchy_breadth_first(child_str_no_mor3);
        rt.assert_that<eq_sets>(empty_test, std::vector<std::string>{});

        rt.info() << "Child resolve renders empty set...\n";
        rt.assert_that<eq_sets>(
            src::of_container(src)
            >> then::hierarchy_breadth_first(
                [](const std::string& s) {return src::null<const std::string&>(); }),
            src,
            OP_CODE_DETAILS("Must coincedence with source set")
            );
        
    }
    
    /** 
    * Test purpose only implementation of Graph. In the depth it is the list of nodes (represented
    * as a vector of strings) and adjacency matrix (vector of unordered_set containing index of nodes)
    */
    class SimpleGraph
    {
        using vertices_t = std::unordered_map< std::string, size_t>;
        using edge_set_t = std::unordered_set<size_t>;//std::pair<size_t, size_t>
        using vertices_presence_t = std::unordered_set<size_t>;//std::pair<size_t, size_t>
        using presence_ptr = std::shared_ptr<vertices_presence_t>;
        using nidx2presence_t = std::pair<size_t, presence_ptr>;

        /** retrieve nodes adjacent with `pair.first`. Result is filtered from nodes that are
        * already presenting in `pair.second` unordered-set
        * \tparam TAdjacencySrc - std::pair<size_t - as a node-index, presence_ptr - unordered_set of 
        *       already visited nodes>
        */
        template <class TAdjacencySrc>
        auto resolve_children_with_loop_prevention(const TAdjacencySrc& pair) const
        {
            return
                src::of_container(std::cref(_adjacency[pair.first]))//take all adjacent node-indexes 
                // ==> Impl with separate methods for filter & map ::::
                //>> then::filter([presence = pair.second](size_t ni2)->bool {
                ////prevent dead-loops by adding already passed nodes to unordered_set
                //        return presence->insert(ni2).second; })
                //>> then::mapping([presence = pair.second](size_t ni3) {
                //        return nidx2presence_t{ ni3, presence };
                //    })
                //// <== Impl with separate methods for filter & map

                >> then::maf_cv([presence = pair.second](size_t nidx, nidx2presence_t& result)->bool {
                    //prevent dead-loops by adding already passed nodes to unordered_set
                    if (presence->insert(nidx).second)
                    {
                        result.first = nidx;
                        result.second = presence;
                        return true;
                    }
                    return false; //filter-out
                })
                ;
        }
    public:
        /** Construct graph from the list of node names and adjacency between them.
        * \param vertices, 
        * \param adjacency - some container that corresponds by index to node from `vertices` 
        *       and contains unordered_set<size_t> of references to another node
        */ 
        template <class TAdjacencyRender>
        SimpleGraph(std::initializer_list<std::string> vertices, TAdjacencyRender adjacency)
            : _adjacency{ adjacency.begin(), adjacency.end() }
        {
            size_t i = 0;
            for (auto& v : vertices)
                _vertices.emplace(std::move(v), i++);
        }
    
        /** Create full graph where all vertices connected with all other (without self-loop) */
        static SimpleGraph full_graph(std::initializer_list<std::string> vertices)
        {
            size_t n = vertices.size();
            return SimpleGraph{ std::move(vertices),
                src::of_iota<size_t>(0, n)
                >> then::mapping([n](auto i) {
                    typename SimpleGraph::edge_set_t res;
                    for (auto j = 0; j < n; ++j)
                        if (i != j)//prevent self-loop
                            res.insert(j);
                    return res;
                }) };
        }
        
        /** Add bi-directional edge between 2 vertices */
        SimpleGraph& create_edge(const std::string& from, const std::string& to)
        {
            auto nfrom = _vertices[from];
            auto nto = _vertices[to];
            _adjacency[nfrom].emplace(nto);
            _adjacency[nto].emplace(nfrom);
            return *this;
        }

        /** Add multiple named vertices */
        SimpleGraph& create_vertices(std::initializer_list<std::string> vertices)
        {
            for (auto& v : vertices)
            {
                size_t n = _vertices.size();
                if (_vertices.emplace(std::move(v), n).second)
                { //name is unique, need reserve place in adjacency matrix
                    _adjacency.push_back({});
                }
            }
            return *this;
        }

        /** Copy only unique name vertices from `other` graph, and all edges */
        SimpleGraph& copy_from(const SimpleGraph& other)
        {
            std::unordered_map<size_t, size_t> remap_idx;
            for (const auto& [k, nidx] : other._vertices)
            { //copy unique only nodes
                auto ins_res = _vertices.emplace(k, _vertices.size());
                remap_idx[nidx] = ins_res.first->second; //id from other graph mapped to the current
                if (ins_res.second) //brand new node has been added
                    _adjacency.emplace_back(edge_set_t{});
            }
            //recreate edges from `other`
            for (size_t remote_idx = 0; remote_idx < other._adjacency.size(); ++remote_idx)
            {
                const auto& remote_adj = other._adjacency[remote_idx];
                auto local_idx = remap_idx[remote_idx];
                auto& local_adj = _adjacency[local_idx];
                std::transform(remote_adj.begin(), remote_adj.end(),
                    std::inserter(local_adj, local_adj.end()),
                    [&](const auto& remote2) { //casr remote id ti the local
                        return remap_idx[remote2];
                    });
            }
            return *this;
        }

        /** Provide indexes list for vertices specified by name */
        auto vertice_to_index(std::initializer_list<std::string> vertices)
        {
            return src::of_container(std::vector(std::move(vertices))) 
                >> then::mapping([this](const auto& name) {
                        return _vertices[name];
                    });
        }
        template <class T>
        using array1_t = std::array<T, 1>;

        /**
        * Renders flur container that allows iterate all reachable nodes starting from parameter `node`.
        * Implmentation uses deep-first algorithm. Starting node treated as self-reachable, so it 
        * apperars in result
        */
        auto deep_first_adjacent(const std::string& node) const
        {
            using presence_ptr = std::shared_ptr<vertices_presence_t>;

            return
                src::of_lazy_value(
                    [nidx = _vertices.at(node)]( ) {
                        return nidx2presence_t(nidx, presence_ptr{ new vertices_presence_t({ nidx }) });
                    })
                //use std::array of 1 element as repeater storage to avoid twice presence creation
                >> then::repeater<array1_t>() 
                >> then::hierarchy_deep_first([this](const nidx2presence_t& pair) {
                    return resolve_children_with_loop_prevention(pair);
                    })
                >> then::mapping([](const nidx2presence_t& pair) {
                    // convert pair back to node index
                    return pair.first;
                    })
                ;
        }

        /**
        * Renders flur container that allows iterate all reachable nodes starting from parameter `node`.
        * Implmentation uses breadth_first algorithm. Starting node treated as self-reachable, so it
        * apperars in result
        */
        auto breadth_first_adjacent(const std::string& node) const
        {
            return
                src::of_lazy_value(
                    [nidx = _vertices.at(node)]() {
                        return nidx2presence_t(nidx, presence_ptr{ new vertices_presence_t({ nidx }) } );
                    })
                //use std::array of 1 element as repeater storage to avoid twice presence creation
                >> then::repeater<array1_t>() 
                >> then::hierarchy_breadth_first([this](const nidx2presence_t& pair) {
                        return resolve_children_with_loop_prevention(pair);
                    })
                >> then::mapping([](const nidx2presence_t& pair) {
                        // convert pair back to node index
                        return pair.first;
                    })
                ;
        }
        
        vertices_t _vertices;
        std::vector< edge_set_t > _adjacency;

    };

    static const SimpleGraph K3 = SimpleGraph::full_graph( {"K3.a"s, "K3.b"s, "K3.c"s} );
    static const SimpleGraph K5 = SimpleGraph::full_graph( {"K5.a"s, "K5.b"s, "K5.c"s, "K5.d"s, "K5.e"s} );

    void test_HierarchyGraph(OP::utest::TestRuntime& rt)
    {
        using namespace std::string_literals;
        //render simple complete graph K5
        SimpleGraph g1 = K5;
        const auto n_island1 = "island1"s, n_island2 = "island2"s,
            n_island3 = "island3"s;
        g1.create_vertices({ n_island1, n_island2, n_island3 });
        //iterate all from single node
        {
            rt.debug() << "Deep first...\n";
            auto dfaj = g1.deep_first_adjacent("K5.a");
            //for (auto x : dfaj)
            //    rt.debug() << std::hex << x << "\n";
            rt.assert_that<eq_unordered_sets>(dfaj, std::vector<size_t>{0, 1, 2, 3, 4});
            //try another node as a start position
            rt.assert_that<eq_unordered_sets>(g1.deep_first_adjacent("K5.c"), std::vector<size_t>{0, 1, 2, 3, 4});
        }
        {
            rt.debug() << "Breadth first...\n";
            auto bfaj = g1.breadth_first_adjacent("K5.a");
            //for (auto x : bfaj)
            //    rt.debug() << std::hex << x << "\n";
            rt.assert_that<eq_unordered_sets>(bfaj, std::vector<size_t>{0, 1, 2, 3, 4});
            //try another node as a start position
            rt.assert_that<eq_unordered_sets>(g1.breadth_first_adjacent("K5.c"), std::vector<size_t>{0, 1, 2, 3, 4});
        }
        //attach some extras verticies to make graph more ramified
        g1
            .copy_from(K3)
            //now create bridge betwee K5.e <-> K3.a
            .create_edge("K5.e", "K3.a")
            //add some unconnected islands
            .create_vertices({"0.a", "0.b"})
            ;
        auto expected_indexes = g1.vertice_to_index({
            "K5.a", "K5.b", "K5.c", "K5.d", "K5.e", "K3.a", "K3.b", "K3.c"
            });

        //for (auto x : g1.deep_first_adjacent("K5.a"))
        //    rt.debug() << std::hex << x << ", ";
        //rt.debug() << "\n";
        //for (auto x : expected_indexes)
        //    rt.debug() << std::hex << x << ", ";
        //rt.debug() << "\n";

        //need twice check to ensure presence-check restarted each time pipeline starts
        auto adjacence_df = g1.deep_first_adjacent("K5.a");
        rt.assert_that<eq_unordered_sets>(adjacence_df, expected_indexes);
        rt.assert_that<eq_unordered_sets>(adjacence_df, expected_indexes);

        auto adjacence_brf = g1.breadth_first_adjacent("K5.a");
        rt.assert_that<eq_unordered_sets>(adjacence_brf, expected_indexes);
        rt.assert_that<eq_unordered_sets>(adjacence_brf, expected_indexes);

        //test island nodes
        rt.assert_that<eq_unordered_sets>(g1.deep_first_adjacent(n_island1), 
            g1.vertice_to_index({ n_island1 }));

        rt.assert_that<eq_unordered_sets>(g1.breadth_first_adjacent(n_island1),
            g1.vertice_to_index({ n_island1 }));

    }
    
 
    static auto& module_suite = OP::utest::default_test_suite("flur.then")
        .declare("hierarchy-deep-first-simple", test_HierarchyDeepFirstSimple)
        .declare("hierarchy-deep-first", test_HierarchyDeepFirst)
        .declare("hierarchy-breadth-first", test_HierarchyBreadthFirst)
        .declare("hierarchy-graph", test_HierarchyGraph)
        ;
}