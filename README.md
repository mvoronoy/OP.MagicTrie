# OP.MagicTrie - C++ Header-Only Libraries

A collection of high-efficiency C++17 header-only libraries for data processing, storage, and testing.

## Overview

This project provides several independent libraries:

| Library | Description |
|---------|-------------|
| **[common](impl/op/common/)** | Reusable shared utilities (type traits, strings, threading, etc.) |
| **[flur](impl/op/flur/)** | Functional lazy-evaluated pipeline for data processing |
| **[trie](impl/op/trie/)** | Persistent Trie data structure for key-value storage |
| **[utest](impl/op/utest/)** | High-efficiency unit testing framework |
| **[vtm](impl/op/vtm/)** | Virtual Transactional Memory using memory mapping |

## Quick Start

All libraries are header-only. Simply include the necessary headers and compile with C++17.

### Dependencies

- **Boost.Interprocess** - Required for trie persistence and vtm
- **C++17** - Minimum compiler requirement

### Build Configuration

If your project doesn't explicitly link Boost.DateTime, add the following define:
```cmake
add_definitions(-DBOOST_DATE_TIME_NO_LIB)
```

## Libraries

### [OP.flur](impl/op/flur/) - Functional Lazy Pipeline

Build functional lazy-evaluated data processing pipelines inspired by .NET and Java Streams.

```cpp
#include <op/flur/flur.h>

std::vector<int> data{1, 2, 3, 4, 5};
auto result = OP::flur::src::of_container(data)
    >> OP::flur::then::mapping([](auto n) { return n * n; })
    >> OP::flur::then::filter([](auto n) { return n % 2 == 0; });
```

### [OP.trie](impl/op/trie/) - Persistent Trie

High-performance key-value storage based on Trie data structure with O(k) complexity (k = key length).

```cpp
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>

auto manager = OP::vtm::BaseSegmentManager::create_new("data.dat");
using Trie = OP::trie::Trie<OP::vtm::SegmentManager, std::string>;
auto trie = Trie::create_new(manager);

trie->insert("hello", "world");
auto pos = trie->find("hello");
```

### [OP.utest](impl/op/utest/) - Unit Testing

Lightweight unit testing framework with fixtures, assertions, and performance measurement.

```cpp
#include <op/utest/unit_test.h>

static auto& suite = OP::utest::default_test_suite("MyTests")
    .declare("test_case", [](OP::utest::TestRuntime& t) {
        t.assert_that<OP::utest::equals>(1, 1);
    });
```

### [OP.vtm](impl/op/vtm/) - Virtual Transactional Memory

Memory-mapped transactional access to disk data with ACID properties.

### [OP.common](impl/op/common/) - Common Utilities

- **Utils.h** - Type utilities, tuple operations, memory alignment
- **FixedString.h** - Fixed-size string implementation
- **Range.h** - Range/interval utilities
- **Bitset.h** - Efficient bitset operations
- **Currying.h** - Function currying support
- **CmdLine.h** - Command-line argument parsing
- **ThreadPool.h** - Thread pool management

## Examples

See the [tests](tests/) directory for comprehensive examples of each library:

- [Trie tests](tests/trie/)
- [Flur tests](tests/flur/)
- [Common tests](tests/common/)
- [Vtm tests](tests/vtm/)

## Build & Setup

The project uses CMake for building tests:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## License

See LICENSE file for details.
