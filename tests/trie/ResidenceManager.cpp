#include <latch>

#include <op/utest/unit_test.h>
#include <op/trie/Trie.h>

#include <op/common/ThreadPool.h>
#include <op/common/DependencyManager.h>
#include <op/common/Currying.h>
#include <op/common/FixedString.h>

#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/vtm/AppendOnlyMemoryPool.h>

#include <op/trie/ResidenceManager.h>


namespace {

    using namespace OP::utest;
    using namespace OP::trie;

    void test_residence_basic(OP::utest::TestRuntime& result)
    {
        using namespace OP::vtm;
        const char seg_file_name[] = "vtm.test";

        using short_string_t = 
            OP::FixedString<OP::fix_str_policy_noexcept<char, 128>>;

        std::array<short_string_t, ResidenceManager::grow_size_c * 2+1> rand_strs;
        for(auto&buffer: rand_strs)
        {
            result.randomizer().next_alpha_num(buffer, 64, 16);
        }
        
        constexpr auto s_size = [](const auto& str)->OP::vtm::segment_pos_t { return static_cast<OP::vtm::segment_pos_t>(str.size()); };

        const short_string_t& test_str1 = rand_strs[0];
        const short_string_t& test_str2 = rand_strs[1];
        const short_string_t& test_str3 = rand_strs[2];

        OP::utils::ThreadPool threads;

        FarAddress resvc_start_address, pool_start_address;
        
        { //scope of testing creation
            OP::common::ServiceRegistry registry;

            auto sm = BaseSegmentManager::create_new(seg_file_name,
                SegmentOptions().segment_size(0x110000));
            auto& dep_graph = registry.resolve_dependencies(
                std::ref(threads),
                std::ref(*sm),
                std::make_unique<AppendOnlyMemoryPool>()
            );

            auto& pool = dep_graph.get<AppendOnlyMemoryPool>();
            pool_start_address = pool.root_address();

            auto residence_manager = ResidenceManager::create(*sm, pool);
            resvc_start_address = residence_manager.root_address();

            //make several allocations
            auto addr1 = pool.allocate(s_size(test_str1));
            sm->writable_block(addr1, s_size(test_str1)).byte_copy(test_str1.data(), s_size(test_str1));
            
            auto addr2 = pool.allocate(s_size(test_str2));
            sm->writable_block(addr2, s_size(test_str2)).byte_copy(test_str2.data(), s_size(test_str2));

            //test service-id must be unique
            residence_manager.add_record(1, addr1);
            result.assert_exception<std::invalid_argument>([&]() {
                residence_manager.add_record(1, addr1);
            });
            
            residence_manager.add_record(2, addr2);
        }

        { //scope of re-opening

            OP::common::ServiceRegistry registry;

            auto sm = BaseSegmentManager::open(seg_file_name);
            auto& dep_graph = registry.resolve_dependencies(
                std::ref(threads),
                std::ref(*sm),
                std::make_unique<AppendOnlyMemoryPool>(pool_start_address)
            );

            auto& pool = dep_graph.get<AppendOnlyMemoryPool>();
            auto residence_manager = ResidenceManager(*sm, pool, resvc_start_address);

            auto addr1 = residence_manager.find_service(1);
            result.assert_true(addr1.has_value());
            result.assert_true(
                memcmp(
                    sm->readonly_block(*addr1, s_size(test_str1)).pos(),
                    test_str1.data(), s_size(test_str1)) == 0);

            auto addr2 = residence_manager.find_service(2);
            result.assert_true(addr2.has_value());
            result.assert_true(
                memcmp(
                    sm->readonly_block(*addr2, s_size(test_str2)).pos(),
                    test_str2.data(), s_size(test_str2)) == 0);

            auto addr3 = residence_manager.find_service(3); //not yet
            result.assert_false(addr3.has_value());

            addr3 = pool.allocate(s_size(test_str3));
            sm->writable_block(*addr3, s_size(test_str3)).byte_copy(test_str3.data(), s_size(test_str3));

            // test residence can be added after reopening
            residence_manager.add_record(3, *addr3);
            auto addr3_other = residence_manager.find_service(3);
            result.assert_that<equals>(*addr3, *addr3_other);
            
            //test overflowing page size
            for (int i = 3; i < rand_strs.size(); ++i)
            {
                auto service_emulation_addr = pool.allocate(s_size(rand_strs[i]));
                sm->writable_block(service_emulation_addr, s_size(rand_strs[i]))
                    .byte_copy(rand_strs[i].data(), s_size(rand_strs[i]));
                residence_manager.add_record(i+1, service_emulation_addr);
            }
            //now test all emulation are valid
            std::uint64_t service_index = 1;
            for (const auto& service_emulation : rand_strs)
            {
                auto entry = residence_manager.find_service(service_index++);
                
                result.assert_true(entry.has_value());
                result.assert_true(
                    memcmp(
                        sm->readonly_block(*entry, s_size(service_emulation)).pos(),
                        service_emulation.data(), s_size(service_emulation)) == 0
                );
            }
        }

    }
    
    static auto& residence_suite = OP::utest::default_test_suite("residence")
        .declare("basic", test_residence_basic)
        //.declare("mt", test_reside)
        ;

}//ns:
