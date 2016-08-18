// matrix.cpp : Defines the entry point for the console application.
//

#include <op/trie/Trie.h>
#include <op/vtm/TransactedSegmentManager.h>
//#include <boost/endian/conversion.hpp>
//n = boost::endian::native_to_big(n);
using namespace OP::trie;

struct Pack
{
    Pack& text(const std::string& s)
    {
        _result.append(s.begin(), s.end());
        return *this;
    }
    Pack& number(std::uint32_t n)
    {
        _result.append(4, 0);
        auto p = _result.rbegin();
        //place high bits first
        *p = static_cast<std::uint8_t>((n >> 24) & 0xFF);
        *++p = static_cast<std::uint8_t>((n >> 16) & 0xFF);
        *++p = static_cast<std::uint8_t>((n >> 8) & 0xFF);
        *++p = static_cast<std::uint8_t>(n & 0xFF);
        return *this;
    }
    const atom_string_t& str() const
    {
        return _result;
    }
private:
    atom_string_t _result;
};
struct MatrixMeta
{

};
template <class Trie, class Number = float>
class Matrix
{
public:
    typedef Trie trie_t;

    Matrix(std::shared_ptr<Trie> trie, atom_string_t prefix)
        : _trie(trie)
        , _prefix(prefix)
    {}

private:
    std::shared_ptr<trie_t> _trie;
    atom_string_t _prefix;
};
template <class SegmentManager, class Number = float>
class MatrixManager
{
public:
    typedef Trie<TransactedSegmentManager, Number> trie_t;
    typedef Matrix<typename trie_t, Number> matrix_t;
    MatrixManager(std::shared_ptr<SegmentManager> segment_manager, atom_string_t prefix = {})
        : _prefix(prefix)
    {
        _trie = trie_t::create_new(segment_manager);
    }
    std::shared_ptr<matrix_t> create_new(std::uint32_t width, std::uint32_t height)
    {
        //using effect: prefix.id.matrix - iterator(prefix)++ gives prefix.id
        Pack().str(_prefix).
    }
private:
    std::shared_ptr<trie_t> _trie;
    atom_string_t _prefix;
};

int main()
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>("matrix.test",
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    return 0;
}

