# OP.trie - Persistent Trie Data Structure

High-performance key-value storage based on Trie (prefix tree) data structure with O(k) complexity where k is the key length.

## Overview

The Trie library provides a persistent, disk-backed implementation of a Trie data structure. It supports both transactional and non-transactional storage modes, making it suitable for various use cases from in-memory caching to durable storage.

## Features

- **O(k) complexity** - Find, insert, erase, and update operations are O(k) where k is key length
- **Persistent storage** - Memory-mapped disk storage for large datasets
- **Transactional support** - ACID-compliant transactions with shadow buffers
- **Prefix operations** - Efficient prefix-based queries and iterations
- **Custom payloads** - Store any value type as payload

## Quick Start

### Basic Usage

```cpp
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>

// Create a segment manager
auto manager = OP::vtm::BaseSegmentManager::create_new("mytrie.dat",
    OP::vtm::SegmentOptions().segment_size(0x110000));

// Define Trie type with string key and double value
using MyTrie = OP::trie::Trie<OP::vtm::SegmentManager, double>;

// Create or open the trie
auto trie = MyTrie::create_new(manager);

// Insert key-value pairs
trie->insert("hello", 1.0);
trie->insert("world", 2.0);
trie->insert("hell", 3.0);

// Find by exact key
auto pos = trie->find("hello");
if (pos != trie->end()) {
    double value = *pos;  // 1.0
}

// Prefix-based operations
auto prefix = trie->find("hell");
auto range = trie->range(prefix);  // All keys starting with "hell"
```

### With Transactions

```cpp
#include <op/trie/Trie.h>
#include <op/vtm/managers/EventSourcingSegmentManager.h>

// Create transactional segment manager
auto baseManager = OP::vtm::BaseSegmentManager::create_new("transactional.dat");
auto txnManager = std::make_shared<OP::vtm::EventSourcingSegmentManager>(
    baseManager, /* history factory */);

using TransactionalTrie = OP::trie::Trie<OP::vtm::EventSourcingSegmentManager, std::string>;
auto trie = TransactionalTrie::create_new(txnManager);

// Operations are automatically transactional
{
    auto guard = txnManager->begin_transaction();
    trie->insert("key1", "value1");
    trie->insert("key2", "value2");
    guard.commit();
}
```

## Key Concepts

### Trie Structure

A Trie (prefix tree) organizes keys character-by-character at the root level:

```
       h --- e --- l --- l
       |
       w --- o --- r --- l --- d
       |
       a --- b --- c
```

All first-level children ('h', 'w', 'a') are stored directly in the root node.

### Iterator

Iterators provide key-value access:

```cpp
// Iterate all entries
for (auto it = trie->begin(); it != trie->end(); ++it) {
    auto& key = it.key();
    auto& value = *it;
}

// Find and iterate from position
auto start = trie->find("prefix");
for (auto it = start; it != trie->end(); ++it) {
    // Process entries starting with "prefix"
}
```

### Prefix Operations

```cpp
// Insert with prefix
auto parent = trie->insert("parent", 0.0);
trie->prefixed_insert(parent, "child1", 1.0);
trie->prefixed_insert(parent, "child2", 2.0);

// Find all with prefix
auto prefix = trie->find("common_prefix");
auto range = trie->range(prefix);  // Range of all keys starting with prefix

// Erase all with prefix
trie->prefixed_erase_all(prefix);
```

### MixedAdapter

The `MixedAdapter` allows customizing iteration behavior for prefixed ranges. Unlike standard iteration that starts from the lowest key, MixedAdapter enables prefixed behavior where `next()` and `in_range()` operate with respect to a specific prefix:

```cpp
#include <op/trie/MixedAdapter.h>

// Create a mixer that iterates only within a prefix
auto mixer = OP::trie::Mixer(
    OP::trie::PrefixedInRange([](const auto& key) {
        return key.starts_with("prefix");
    })
);

// Use with flur integration
auto range = trie->range() >> OP::flur::then::mapping(
    OP::trie::make_mixer(std::move(mixer)));
```

## API Reference

### Main Classes

| Class | Description |
|-------|-------------|
| `Trie<SegmentManager, Payload>` | Main Trie container |
| `TrieIterator` | Iterator for traversing Trie |
| `PlainValueManager<Payload>` | Simple value storage |
| `MixedAdapter` | Customize prefixed iteration behavior |

### Key Methods

```cpp
// Insert - returns (iterator, bool) pair
auto [it, inserted] = trie->insert(key, value);

// Find - returns iterator
auto it = trie->find(key);

// Update value
trie->update(it, new_value);

// Erase
trie->erase(it);

// Check existence
bool exists = trie->check_exists(key);

// Range iteration
auto range = trie->range();           // All entries
auto range = trie->range(prefix);      // Entries with prefix

// Lower bound
auto it = trie->lower_bound(key);     // First >= key
```

### Storage Backends

| Backend | Description |
|---------|-------------|
| `SegmentManager` | Non-transactional, direct memory-mapped I/O |
| `EventSourcingSegmentManager` | Transactional with event sourcing |

## Advanced Topics

### Custom Value Managers

Implement your own value manager for specialized storage:

```cpp
struct CustomValueManager {
    using payload_t = MyData;
    
    // Required methods for read/write
};
```

### Long Keys

The Trie efficiently handles very long keys through internal stem encoding:

```cpp
std::string veryLongKey(1000, 'a');  // 1000 character key
trie->insert(veryLongKey, value);
```

### Thread Safety

- Single-writer, multiple-reader pattern
- Use transactions for coordinated multi-key operations

## Examples

See [tests/trie/](tests/trie/) for comprehensive examples:

- Basic CRUD operations
- Prefix-based queries
- Transaction usage
- Iterator behavior
- Performance benchmarks

## Dependencies

- C++17
- Boost.Interprocess (for memory mapping)
- OP.vtm (for segment management)
- OP.common (for atom strings)
