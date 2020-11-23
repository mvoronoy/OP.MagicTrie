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
Project uses only include files. 
