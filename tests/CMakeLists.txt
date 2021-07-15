# CMakeList.txt : CMake project for stand-alone test of OP
#
cmake_minimum_required (VERSION 3.8)
project(trie_test)

set(BOOST_ROOT $ENV{BOOST_ROOT})
# per doc:
#       Boost.Interprocess depends on Boost.DateTime, which needs separate compilation. However, 
#       the subset used by Boost.Interprocess does not need any separate compilation so the user 
#       can define BOOST_DATE_TIME_NO_LIB to avoid Boost from trying to automatically link the Boost.DateTime
add_definitions(-DBOOST_DATE_TIME_NO_LIB)
find_package(Boost REQUIRED)
set(Boost_DEBUG 1)

IF (Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
    ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()

message("~*~*~*~*~*~*~*~*~*~*~*")
message("boost inc:${Boost_INCLUDE_DIRS}")

find_package(Threads REQUIRED)

enable_testing()

# Add source to this project's executable.
add_executable (trie_test 
    
    "BgWorkerTest.cpp"
    "BitsetTest.cpp"

    "common/UtilsTest.cpp"
    "common/ZoneTest.cpp"
    "common/ThreadPool.cpp"

    "ContainersTest.cpp"
    "FixedSizeMemoryManagerTest.cpp"
    "ranges/CommonRangesOpTest.cpp"
    "SegmentManagerTest.cpp"
    "SegmentToplogyTest.cpp"
    "testMixedAdapter.cpp"
    "tr.cpp"
    "Transacted.cpp"
    "EventSourcedSegmentManagerTest.cpp"
    "Trie.cpp"
    "SpanContainerTest.cpp"

    "flur/StlContainers.cpp" 
    "flur/ThenDefault.cpp" 
    "flur/Mt.cpp" 
    "flur/Cartesian.cpp"
    "flur/Generator.cpp"
    "flur/ThenFlatMap.cpp"
    "flur/Minibatch.cpp"
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
set(TR_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../impl )

target_include_directories(trie_test
    PUBLIC
        ${TR_HOME}
    )

target_link_libraries(${PROJECT_NAME} Threads::Threads)

# Unit tests
add_test(NAME "OP.trie.tests.override_join_range" COMMAND trie_test -r "Trie/override_join_range")
add_test(NAME "root.OP.tests.all" COMMAND trie_test -r ".+/.+")
# add_test(NAME "ABC.OP.namespace.Tests.SomeTest" COMMAND trie_test -r ".+/.+")