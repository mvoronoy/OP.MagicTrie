# OP.MagicTrie

## Persisted Trie generic container

The goal of project is to give C++ developers very fast key-value container based on [Trie](https://en.wikipedia.org/wiki/Trie) . 
One of the wonderful thing about Trie that complexity for most operations to find, insert, erase and update estimated as `O(m)`, where m is a length of used key. 
This project gives ability to developer to store key-value pairs in transacted way in huge storage on the disk.

# Target platforms
The project uses C++11 standart template library (STL) and [Boost.Interprocess](http://www.boost.org/doc/libs/develop/doc/html/interprocess.html). This restrict supported
platforms. So from Boost support list:

- Windows Vista, Windows 7. MSVC 13.0 and newer.
- Linux. GCC 4.5 and newer. Older versions may work too, but it was not tested.
- Linux. Intel C++ 13.1.0.146 Build 20130121.
- Linux. Clang 3.2.


# Planned features:
-Nearest release 0.1 to introduce Trie container;
- Series of articles with common title “Magic of Trie” to explain different techniques and algorithms to process big amount of information with help of Tries. 
