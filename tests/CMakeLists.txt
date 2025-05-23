﻿# CMakeList.txt : CMake project for stand-alone test of OP
#
cmake_minimum_required (VERSION 3.20)
project(trie_test)

if(MSVC)
#    add_compile_options("/P" )
endif(MSVC)

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

# Allow /P preprocess file   
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /P")
#set_source_files_properties("tests/tr.cpp" PROPERTIES COMPILE_FLAGS /P)


# Add source to this project's executable.
add_executable (trie_test 
    
    "common/BitsetTest.cpp"
    "common/UtilsTest.cpp"
    "common/CmdLine.cpp"
    "common/ZoneTest.cpp"
    "common/ThreadPool.cpp"
    "common/Currying.cpp"
    "common/Multiimplementation.cpp"
    "common/PreallocCache.cpp"
    "common/basic.cpp"
    "common/FixedString.cpp"

    "vtm/ContainersTest.cpp"
    "vtm/FixedSizeMemoryManagerTest.cpp"
    "vtm/StringManagerTest.cpp"
    "vtm/SegmentManagerTest.cpp"
    "vtm/SegmentToplogyTest.cpp"

    "SpanContainerTest.cpp"
    "tr.cpp"

    "vtm/Transacted.cpp"
    "vtm/EventSourcedSegmentManagerTest.cpp"
    
    "trie/Trie.cpp"
    "trie/TrieRanges.cpp"
    "trie/testMixedAdapter.cpp"
    "trie/trieValueManager.cpp"
    "trie/TrieFixedString.cpp"
    

    "flur/StlContainers.cpp" 
    "flur/ThenDefault.cpp" 
    "flur/Mt.cpp" 
    "flur/Filter.cpp" 
    "flur/maf.cpp" 
    "flur/Cartesian.cpp"
    "flur/Zip.cpp"
    "flur/Generator.cpp"
    "flur/ThenFlatMap.cpp"
    "flur/ThenMap.cpp"
    "flur/ThenOrderingFlatMap.cpp"
    "flur/Minibatch.cpp"
    "flur/ThenException.cpp" 
    "flur/ThenDistinct.cpp"
    "flur/ThenDiff.cpp"
    "flur/Polymorphic.cpp" 
    "flur/StringInput.cpp"
    "flur/Join.cpp"
    "flur/UnionAll.cpp"
    "flur/MergeSortUnion.cpp"
    "flur/Iterators.cpp"
    "flur/ThenHierarchy.cpp"
    "flur/OfLazyValue.cpp"
    "flur/JoinUnordered.cpp"
    "flur/ParallelSort.cpp"
    "flur/Applicator.cpp"
 
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