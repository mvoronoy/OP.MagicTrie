# OP.MagicTrie

## Persisted Trie generic container

The goal of project to give C++ developers fast key-value container based on [Trie](https://en.wikipedia.org/wiki/Trie). Disk storage allows you organize petabytes of information. 
One of the wonderful thing about Trie that complexity of main operations are simple. Find, insert, erase and update are estimated as constantn `O(c)`, but varies of length of used key. 
The library gives a choice for developer between transactional or non-transactional storage.
# Target platforms
The project uses C++11 standart template library (STL) and [Boost.Interprocess](http://www.boost.org/doc/libs/develop/doc/html/interprocess.html). This restricts supported
platforms. So see Boost support [list](https://www.boost.org/doc/libs/1_74_0/libs/compatibility/index.html):


# Planned features:
- Nearest release 0.1 to introduce Trie container;
- Series of articles with common title “Magic of Trie” to explain different techniques and algorithms to process big amount of information with help of Tries. 

# Build & Setup
Project depends on [Boost.Interprocess](http://www.boost.org/doc/libs/develop/doc/html/interprocess.html) to manage trie persistence. No additional compilation is needed.
Project uses only include files. If your project doesn't explictly linking Boost.DateTime then linker may generate the error about missing Boost.DateTime library. Then follow Boost documentation:

>       Boost.Interprocess depends on Boost.DateTime, which needs separate compilation. However, 
>       the subset used by Boost.Interprocess does not need any separate compilation so the user 
>       can define BOOST_DATE_TIME_NO_LIB to avoid Boost from trying to automatically link the Boost.DateTime
See example in $/tests/CMakeLists.txt for `add_definitions(-DBOOST_DATE_TIME_NO_LIB)`

#OP.Trie
## Transaction management
Trie is highly customizable template based implementation. All storage implementation is delegated to template parameter `TSegmentManager` in:
```c++
        template <class TSegmentManager, class Payload, std::uint32_t initial_node_count = 1024>
        struct Trie : public std::enable_shared_from_this< Trie<TSegmentManager, Payload, initial_node_count> >

```
Vanila `OP::trie::SegmentManager` provides immediate exchange with disk by memory mapping. As an alternative you can use OP::trie::TransactedSegmentManager.
This class provides:

* Isolation level: ["Read Committed"](https://en.wikipedia.org/wiki/Isolation_(database_systems)#Read_committed)
* Multiple transactions initiate read-lock on the same memory region
* Only single transaction can lock a memory region for the write

Implementation is based on creating of shadow-buffers that aggregate changes in memory and on commit flushes to the result virtual memory