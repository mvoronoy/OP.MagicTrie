# OP.utest - Unit Testing Framework

A lightweight, high-efficiency C++17 unit testing framework with fixtures, assertions, and performance measurement.

## Overview

OP.utest is a header-only unit testing framework designed for simplicity and performance. It provides:
- Test suite organization
- Flexible assertion system with custom markers
- Fixtures with setup/teardown
- Performance measurement (load testing)
- Event system for test lifecycle

## Quick Start

### Basic Test

```cpp
#include <op/utest/unit_test.h>

static auto& suite = OP::utest::default_test_suite("MySuite")
    .declare("simple_test", [](OP::utest::TestRuntime& t) {
        t.assert_that<OP::utest::equals>(1, 1);
    });
```

## Test with Assertions

```cpp
#include <op/utest/unit_test.h>

void test_assertions(OP::utest::TestRuntime& t)
{
    // Simple boolean assertions
    t.assert_true(2 + 2 == 4);
    t.assert_false(1 == 2);
    
    // Equality
    t.assert_that<OP::utest::equals>(42, 42);
    
    // Comparison
    t.assert_that<OP::utest::less>(5, 10);
    t.assert_that<OP::utest::greater>(10, 5);
    
    // Collections
    std::vector<int> a{1, 2, 3};
    std::vector<int> b{1, 2, 3};
    t.assert_that<OP::utest::eq_sets>(a, b);
    
    // Floating point
    t.assert_that<OP::utest::almost_eq>(3.14, 3.14159, 0.001);
}
```

## Assertion Markers

The framework provides various assertion markers:

| Marker | Description |
|--------|-------------|
| `equals` | Exact equality |
| `not_equals` | Inequality |
| `less` | a < b |
| `greater` | a > b |
| `less_or_equals` | a <= b |
| `greater_or_equals` | a >= b |
| `eq_sets` | Equal sets (ordered) |
| `eq_unordered_sets` | Equal sets (unordered) |
| `eq_ranges` | Equal ranges |
| `almost_eq` | Floating point equality with epsilon |
| `negate<T>` | Negate any marker |
| `logical_not<T>` | Logical negation |
| `regex_match` | Full regex match |
| `regex_search` | Partial regex match |
| `is_null` | Null pointer check |
| `is_not_null` | Non-null pointer check |

### Custom Markers

Create custom assertion markers:

```cpp
struct is_positive {
    constexpr static size_t args_c = 1;
    
    template<class T>
    constexpr bool operator()(T v) const {
        return v > 0;
    }
};

// Usage
t.assert_that<is_positive>(42);
```

## Fixtures

### Simple Fixture

```cpp
static auto& suite = OP::utest::default_test_suite("MySuite")
    .with_fixture(
        []() -> std::any { 
            // Create and return fixture state
            MyData data;
            data.value = 42;
            return std::any(std::in_place_type<MyData>, std::move(data));
        },
        [](std::any& fixture) {
            // teardown - optional
            // fixture holds the std::any from setup
        }
    )
    .declare("test_with_fixture", [](MyData& data, OP::utest::TestRuntime& t) {
        t.assert_that<OP::utest::equals>(data.value, 42);
    });
```

### Fixture with Setup Only

```cpp
static auto& suite = OP::utest::default_test_suite("MySuite")
    .with_fixture(
        []() -> std::any { 
            return std::any(std::in_place_type<MyClass>, constructor_args...); 
        }
    )
    .declare("test", [](const MyClass& obj) {
        // use obj
    });
```

## Test Options

### Configure Test Run

```cpp
OP::utest::TestRunOptions options;
options.fail_fast(true);
options.log_level(OP::utest::ResultLevel::debug);
options.random_seed(42);

// Run tests
OP::utest::TestRun::default_instance().run_all();
```

### Load Testing (Performance Measurement)

```cpp
OP::utest::TestRunOptions options;
options.load_run({.warm_up = 10, .runs = 100});

// Test function receives warm-up calls, then measures execution time
static auto& suite = OP::utest::default_test_suite("PerfSuite")
    .declare("bench", [](OP::utest::TestRuntime& t) {
        // This runs warm_up times, then runs 'runs' times
        // Result includes average execution time
    });
```

## Test Suite Features

### Tagging

```cpp
static auto& suite = OP::utest::default_test_suite("TaggedSuite")
    .declare("quick_test", [](OP::utest::TestRuntime& t) {
        // ...
    }, "quick", "smoke")  // Tags
    .declare("slow_test", [](OP::utest::TestRuntime& t) {
        // ...
    }, "slow");
```

### Exception Testing

```cpp
t.assert_exception<std::runtime_error>([]() {
    throw std::runtime_error("expected");
}, "Exception should be thrown")
.then([](const std::runtime_error& e) {
    // Additional checks
});
```

### Test Runtime API

```cpp
void my_test(OP::utest::TestRuntime& t)
{
    // Logging
    t.info() << "Information message\n";
    t.debug() << "Debug message\n";
    t.error() << "Error message\n";
    
    // Unconditional fail
    t.fail("This test failed");
    
    // Random generation
    auto& rng = t.randomizer();
    int value = rng.next_in_range(0, 100);
    
    // Measure execution time
    double ms = t.measured_run([]() {
        // code to measure
    }, 10, 2);  // 10 runs, 2 warm-up
}
```

## Command-Line Integration

```cpp
#include <op/utest/cmdln_unit_test.h>

int main(int argc, char** argv)
{
    return OP::utest::cmdln_unit_test::run(argc, argv);
}
```

### Command-Line Options

| Option | Description |
|--------|-------------|
| `-h`, `--help`, `-?` | Show usage |
| `-l` | List known test cases instead of run |
| `-d` | Set logging level: 1=error (default), 2=info, 3=debug |
| `-rs <regex>` | Regex to match Test Suites to run |
| `-rc <regex>` | Regex to match Test Cases to run |
| `-rf <regex>` | Regex to match Test Fixtures to run |
| `-t`, `-tw <regex>` | White-list tag regex |
| `-tb <regex>` | Black-list tag regex |
| `-n <N>` | Run test in bulk mode N times |
| `-s <seed>` | Set random seed for reproducibility |
| `-f` | Fail fast - stop on first failure |
| `-j` | Render JSON report |

### Examples

```bash
./tests -rs ".+"              # Run all test suites
./tests -rs "test-[1-9]"     # Run suites test-1 through test-9
./tests -rc "test-[^1-3]"    # Run test cases excluding test-1, test-2, test-3
./tests -t "long"             # Run only tests tagged 'long'
./tests -tb "broken"          # Exclude tests tagged 'broken'
./tests -n 100                # Run tests 100 times
./tests -s 42                 # Run with seed 42
./tests -f                    # Fail fast mode
./tests -j                    # JSON report output
```

## Complete Example

```cpp
#include <op/utest/unit_test.h>
#include <vector>
#include <algorithm>

struct SortedData {
    std::vector<int> data;
    
    void setup() {
        data = {5, 2, 8, 1, 9};
        std::sort(data.begin(), data.end());
    }
};

void test_sorted(OP::utest::TestRuntime& t, SortedData& data)
{
    t.assert_that<OP::utest::equals>(data.data.size(), 5u);
    t.assert_that<OP::utest::less>(data.data[0], data.data[4]);  // 1 < 9
}

void test_binary_search(OP::utest::TestRuntime& t, SortedData& data)
{
    bool found = std::binary_search(data.data.begin(), data.data.end(), 5);
    t.assert_true(found, "5 should be found");
}

static auto& suite = OP::utest::default_test_suite("StdAlgo")
    .with_fixture("sorted_data", 
        []() -> std::any { 
            auto data = SortedData();
            data.setup();
            return std::any(std::in_place_type<SortedData>, std::move(data));
        }
    )
    .declare("sort", test_sorted)
    .declare("binary_search", test_binary_search);
```

## Event System

The framework provides events for test lifecycle monitoring. All events are defined in `UnitTestEventSupplier::Code`:

| Event | Description |
|-------|-------------|
| `test_run_start` | Test run started |
| `test_run_end` | Test run finished |
| `suite_start` | Test suite started |
| `suite_end` | Test suite finished |
| `case_start` | Test case started |
| `case_end` | Test case finished |
| `load_execute_warm` | Load test warm-up started |
| `load_execute_run` | Load test execution started |

### Using Events

```cpp
OP::utest::UnitTestEventSupplier& events = test_run.event_supplier();

events.bind<OP::utest::UnitTestEventSupplier::suite_start>([](auto& suite, auto& time) {
    std::cout << "Suite started: " << suite.id() << "\n";
});

events.bind<OP::utest::UnitTestEventSupplier::case_end>([](auto& runtime, auto& fixture, auto& result, auto& time) {
    if (result.status() != OP::utest::TestResult::Status::ok) {
        std::cerr << "Test failed: " << result.test_case()->id() << "\n";
    }
});
```

## Dependencies

- C++17 standard library
- OP.common (for internal utilities)
- No external dependencies
