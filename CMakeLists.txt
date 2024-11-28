# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.16)

project ("OP.MagicTrie")
include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}) 

#
enable_testing()

# Include sub-projects.
add_subdirectory ("tests")

set(UNIT_TEST trie_test)

# Force tests
#add_custom_target(run_unit_test ALL
#    DEPENDS ${UNIT_TEST}
#     COMMENT "Run unit tests..."
#     POST_BUILD 
#    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
#)