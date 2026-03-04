# OP.vtm - Virtual Transactional Memory

Memory-mapped transactional access to disk data with ACID properties.

## Overview

The VTM (Virtual Transactional Memory) library provides:
- Memory-mapped I/O for efficient disk access
- Transaction management with shadow buffers
- Configurable isolation levels: Read Committed (default), Read Uncommitted, or Prevent
- Multiple reader / single writer concurrency

## Key Concepts

### Segment

A segment is a logical mechanism that allows access to a sequentially growing file. Each segment grows within the same file, providing virtual address space for data storage.

```cpp
#include <op/vtm/SegmentManager.h>

// Create new segment file
auto manager = OP::vtm::BaseSegmentManager::create_new("data.dat",
    OP::vtm::SegmentOptions().segment_size(0x110000));
```

### Transaction

Transactions provide atomicity and isolation:

```cpp
{
    auto guard = manager->begin_transaction();
    // Perform operations
    guard.commit();  // Flush changes to disk
} // Automatic rollback if not committed
```

### Read/Write Access

```cpp
// Write access
{
    auto access = manager->accessor<MyStruct>(address);
    access->field = value;
}

// Read access
{
    auto view = manager->view<MyStruct>(address);
    auto value = view->field;
}
```

## Core Classes

| Class | Description |
|-------|-------------|
| `SegmentManager` | Base class for memory management |
| `BaseSegmentManager` | File-based memory mapping implementation |
| `EventSourcingSegmentManager` | Transactional manager with history |
| `TransactionGuard` | RAII wrapper for transaction scope |

## Usage

### Creating a Persistent Segment

```cpp
#include <op/vtm/SegmentManager.h>
#include <op/vtm/managers/BaseSegmentManager.h>

// Create new segment
auto manager = OP::vtm::BaseSegmentManager::create_new(
    "myfile.dat",
    OP::vtm::SegmentOptions()
        .segment_size(0x100000)  // 1MB segments
);

// Open existing
auto manager = OP::vtm::BaseSegmentManager::open("myfile.dat");
```

### Typed Access

```cpp
struct MyData {
    int id;
    double value;
    char name[64];
};

// Allocate and write
FarAddress addr = /* get address */;
{
    auto access = manager->accessor<MyData>(addr);
    access->id = 1;
    access->value = 3.14;
    strcpy(access->name, "test");
}

// Read
{
    auto view = manager->view<MyData>(addr);
    std::cout << view->id << " " << view->value;
}
```

### Transactions

```cpp
// Non-transactional (direct write)
{
    auto access = manager->accessor<MyData>(addr);
    access->value = 42;
    manager->flush();
}

// Transactional (with rollback support)
{
    auto guard = manager->begin_transaction();
    auto access = manager->accessor<MyData>(addr);
    access->value = 42;
    
    if (some_condition) {
        guard.commit();  // Save changes
    }
    // else: automatic rollback
}
```

### Event Sourcing

For full ACID compliance with change history:

```cpp
#include <op/vtm/managers/EventSourcingSegmentManager.h>
#include <op/vtm/managers/InMemMemoryChangeHistory.h>

// Create with in-memory change history
auto baseManager = OP::vtm::BaseSegmentManager::create_new("data.dat");
auto historyFactory = std::make_shared<OP::vtm::InMemMemoryChangeHistory::Factory>();
auto txnManager = std::make_shared<OP::vtm::EventSourcingSegmentManager>(
    baseManager, historyFactory);

// All operations are transactional
auto trie = OP::trie::Trie::create_new(txnManager);
```

## Segment Manager API

### Methods

```cpp
// Transaction management
transaction_ptr_t begin_transaction();
void commit();
void rollback();

// Memory access
ReadonlyMemoryChunk readonly_block(FarAddress pos, segment_pos_t size);
MemoryChunk writable_block(FarAddress pos, segment_pos_t size);

// Typed access helpers
template<class T> ReadonlyAccess<T> view(FarAddress pos);
template<class T> WritableAccess<T> accessor(FarAddress pos);

// Segment operations
void ensure_segment(segment_idx_t index);
segment_idx_t available_segments();
void flush();
```

## Memory Model

### Address Space

```
Segment 0    Segment 1    Segment 2    ...
+--------+  +--------+  +--------+
| Header |  | Header |  | Header |
+--------+  +--------+  +--------+
| Data   |  | Data   |  | Data   |
|   ...  |  |   ...  |  |   ...  |
+--------+  +--------+  +--------+
```

## Advanced Topics

### Custom Data Structures

VTM provides building blocks for persistent data structures:

```cpp
#include <op/vtm/AppendOnlyLog.h>
#include <op/vtm/AppendOnlySkipList.h>

// Append-only log
AppendOnlyLog<MyEntry> log(manager);
log.append(entry);

// Skip list
AppendOnlySkipList<Key, Value> skipList(manager);
skipList.insert(key, value);
```

### Segment Topology

Custom segment layouts:

```cpp
#include <op/vtm/SegmentTopology.h>

// Define custom topology for your data
struct MyTopology : OP::vtm::SegmentTopology {
    // Custom slot definitions
};
```

### Memory Management

```cpp
#include <op/vtm/slots/FixedSizeMemoryManager.h>
#include <op/vtm/slots/HeapManager.h>

// Fixed-size block allocator
FixedSizeMemoryManager<MyBlock> allocator(manager);
auto addr = allocator.allocate();
allocator.deallocate(addr);

// Heap allocator (variable-size)
HeapManager heap(manager);
auto addr = heap.allocate(size);
heap.deallocate(addr);
```

## Integration with Trie

VTM is commonly used with the Trie library for persistent storage:

```cpp
#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>

auto manager = OP::vtm::BaseSegmentManager::create_new("trie.dat");

using Trie = OP::trie::Trie<
    OP::vtm::SegmentManager, 
    PlainValueManager<double>, 
    OP::common::atom_string_t>;

auto trie = Trie::create_new(manager);
trie->insert("key", 1.0);
```

## Dependencies

- C++17
- Boost.Interprocess
- OP.common (for utilities)
