// Defines the entry point for the unit-tests console application.

#include <iostream>
#include <op/utest/unit_test.h>
#include <op/utest/cmdln_unit_test.h>

int main(int argc, char* argv[])
{
    // Call default console handler of unit tests
    return OP::utest::cmdline::simple_command_line_run(argc, argv);
}
