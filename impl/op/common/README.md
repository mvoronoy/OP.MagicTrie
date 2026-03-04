# OP.common - Common Utilities Library

A collection of reusable C++17 utilities used across all OP libraries.

## Overview

The common library provides foundational utilities including type traits, string handling, threading, and various helper classes.

## Modules

### Type Utilities (Utils.h)

Provides compile-time type manipulation utilities:

```cpp
#include <op/common/Utils.h>

// Type in tuple
using type_idx = OP::utils::get_type_index<int, char, int, void>::value; // 1

// Tuple element access by type
auto& value = OP::utils::tuple_ref_index<T, std::tuple<A, B, T>>::value;

// Type filtering
using filtered = typename OP::utils::TypeFilter<Predicate, std::tuple<A, B, C>>::type;

// Memory alignment
auto aligned = OP::utils::align_on(address, 16);
```

### Fixed Strings (FixedString.h)

Fixed-size string implementation with compile-time length checking:

```cpp
#include <op/common/FixedString.h>

OP::common::FixedString<16> str;  // Max 16 characters
str = "Hello";
str.resize(5);
```

### Range/Interval (Range.h)

Utilities for working with ranges and intervals:

```cpp
#include <op/common/Range.h>

using Range = OP::Range<int>;
Range r(0, 10);  // [0, 10)
bool contains = r.in(5);  // true
```

### Bitset Operations (Bitset.h)

Efficient bitset with compile-time sizing:

```cpp
#include <op/common/Bitset.h>

OP::Bitset<64> bits;
bits.set(5);
bool exists = bits.get(5);
bits.clear(5);
```

### Function Currying (Currying.h)

Support for partial function application:

```cpp
#include <op/common/Currying.h>

auto add = [](int a, int b) { return a + b; };
auto add5 = OP::currying::partial(add, 5);
int result = add5(3);  // 8
```

### Command-Line Parsing (CmdLine.h)

Argument parsing with type safety:

```cpp
#include <op/common/CmdLine.h>

OP::common::CommandLineParser parser(
    OP::common::key("-n"), OP::common::required<int>("count"),
    OP::common::key("-o"), OP::common::desc("output file"), OP::common::stroll()
);
parser.parse(argc, argv);
```

### Thread Pool (ThreadPool.h)

Simple thread pool for parallel task execution:

```cpp
#include <op/common/ThreadPool.h>

OP::common::ThreadPool pool(4);
pool.enqueue([]() { /* work */ });
pool.join();
```

### Event/Subscriber Pattern (EventSupplier.h)

Publish-subscribe event system:

```cpp
#include <op/common/EventSupplier.h>

OP::events::EventSupplier<...> supplier;
auto unsub = supplier.on<event_code>([](const Payload& p) { /* handle */ });
supplier.send<event_code>(payload);
```

### Additional Utilities

| Header | Description |
|--------|-------------|
| `atomic_utils.h` | Atomic operations and waitable semantics |
| `Exceptions.h` | Error handling and error codes |
| `ValueGuard.h` | RAII guards for value restoration |
| `IoFlagGuard.h` | Stream I/O flag preservation |
| `Pre.h` | Memory pre-allocation utilities |
| `StackAlloc.h` | Stack-based allocation |
| `SpanContainer.h` | Span-based container |
| `Console.h` | Cross-platform console coloring |
| `StringEnforce.h` | String validation |

## Usage

Include the specific header needed:

```cpp
#include <op/common/Utils.h>
#include <op/common/FixedString.h>
// etc.
```

## Dependencies

- C++17 standard library
- No external dependencies for core utilities
