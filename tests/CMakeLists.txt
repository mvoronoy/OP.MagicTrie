# CMakeList.txt : CMake project for stand-alone test of OP
#
cmake_minimum_required (VERSION 3.27)
project(trie_test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("/Zc:__cplusplus")
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

if(MSVC)
# Allow /P preprocess file   
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /P")
#set_source_files_properties("tests/tr.cpp" PROPERTIES COMPILE_FLAGS /P)
endif(MSVC)


# Add source to this project's executable.
add_executable ( ${PROJECT_NAME} 

tr.cpp

common/basic.cpp
common/BitBuilder.cpp
common/BitsetTest.cpp
common/CmdLine.cpp
common/ConditionalIterator.cpp
common/Currying.cpp
common/DependencyManager.cpp
common/EventSupplier.cpp
common/FixedString.cpp
common/Multiimplementation.cpp
common/SequenceReorder.cpp
common/ThreadPool.cpp
common/UtilsTest.cpp
common/ZoneTest.cpp

flur/Applicator.cpp
flur/Cartesian.cpp
flur/Filter.cpp
flur/Generator.cpp
flur/Iterators.cpp
flur/Join.cpp
flur/JoinUnordered.cpp
flur/maf.cpp
flur/MergeSortUnion.cpp
flur/Mt.cpp
flur/OfLazyValue.cpp
flur/ParallelSort.cpp
flur/Polymorphic.cpp
flur/StlContainers.cpp
flur/StringInput.cpp
flur/ThenDefault.cpp
flur/ThenDiff.cpp
flur/ThenDistinct.cpp
flur/ThenException.cpp
flur/ThenFlatMap.cpp
flur/ThenHierarchy.cpp
flur/ThenMap.cpp
flur/ThenOrderingFlatMap.cpp
flur/UnionAll.cpp
flur/Zip.cpp

lexico/testGolomb.cpp
lexico/testLexico.cpp

trie/BitIndexedVector.cpp
trie/ResidenceManager.cpp
trie/StringManagerTest.cpp
trie/testMixedAdapter.cpp
trie/Trie.cpp
trie/TrieAllocatorImpl.cpp
trie/TrieFixedString.cpp
trie/TrieMemoryServiceImpl.cpp
trie/TrieRanges.cpp
trie/trieValueManager.cpp

vtm/AppendAtomicList.cpp
vtm/AppendOnlyLog.cpp
vtm/AppendOnlySkipList.cpp
vtm/InMemMemoryChangeHistory.cpp
vtm/SizeRadixRoutingIndex.cpp

)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
set(TR_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../impl )

target_include_directories(trie_test
    PUBLIC
        ${TR_HOME}
    )

target_link_libraries(${PROJECT_NAME} Threads::Threads)

if(MSVC)
target_link_options(trie_test PRIVATE "/PROFILE")
endif(MSVC)

# Unit tests
add_test(NAME "root.OP.tests.all" COMMAND trie_test -j -f)
# add_test(NAME "ABC.OP.namespace.Tests.SomeTest" COMMAND trie_test -r ".+/.+")
