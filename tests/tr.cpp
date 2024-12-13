    // trie.cpp : Defines the entry point for the console application.
//
#define _SCL_SECURE_NO_WARNINGS 1

#include <iostream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/trie/Containers.h>

#include <set>
#include <cstdint>
#include <cassert>
#include <iterator>

#include <ctime>
#include <chrono>
#include <regex>

#include <op/utest/unit_test.h>
#include <op/utest/cmdln_unit_test.h>

int main(int argc, char* argv[])
{
    return OP::utest::cmdline::simple_command_line_run(argc, argv);
}
