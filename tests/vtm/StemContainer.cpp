#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/trie/StemContainer.h>
#include <op/common/typedefs.h>

using namespace OP::trie::stem;
using namespace OP::trie;

void test_StemStoreSimple(OP::utest::TestRuntime &tresult)
{
    //auto tst1 = std::unique_ptr<StemStore>(OP::trie::StemStore::create(1, 2));
    //tresult.assert_true(tst1->width() == 1 && tst1->height() == 2, OP_CODE_DETAILS());
    //tresult.assert_true(tst1->size() == 0, OP_CODE_DETAILS());
    //tresult.assert_true(tst1->length(0) == 0, OP_CODE_DETAILS());
    //const OP::trie::StemStore::atom_t seq1[]  = { 'a' };
    //auto b_seq1 = std::begin(seq1);
    //tresult.assert_true(0 == tst1->accommodate(b_seq1, std::end(seq1)), OP_CODE_DETAILS());
    //tresult.assert_true(tst1->size() == 1, OP_CODE_DETAILS());
    //tresult.assert_true(tst1->length(0) == 1, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq1 == std::end(seq1), OP_CODE_DETAILS());
    //auto r1 = tst1->chain_at(0);;
    //tresult.assert_true( OP::utest::tools::range_equals(r1.first, r1.second, std::begin(seq1), std::end(seq1)) , OP_CODE_DETAILS());
    //const OP::trie::StemStore::atom_t seq2[]  = { 'b', 'c' };
    //auto b_seq2 = std::begin(seq2);
    //tresult.assert_true(OP::trie::StemStore::nil_c == tst1->accommodate(b_seq2, std::end(seq2)), OP_CODE_DETAILS());
    //tresult.assert_true(tst1->size() == 1, OP_CODE_DETAILS());
    //tst1->erase(0);;
    //tresult.assert_true(tst1->size() == 0, OP_CODE_DETAILS());
    //tresult.assert_true(0 == tst1->accommodate(b_seq2, std::end(seq2)), OP_CODE_DETAILS());
    //tresult.assert_true(tst1->size() == 1, OP_CODE_DETAILS());
    //tresult.assert_true(tst1->length(0) == 2, OP_CODE_DETAILS());
    //auto r2 = tst1->chain_at(0);;
    //tresult.assert_true(!std::lexicographical_compare(r2.first, r2.second, std::begin(seq2), std::end(seq2))
    //    && !std::lexicographical_compare(std::begin(seq2), std::end(seq2), r2.first, r2.second), OP_CODE_DETAILS());
}
inline dim_t index_of_accomodate(dim_t ident)
{
    return ident;
}
inline dim_t index_of_accomodate(const std::pair<dim_t, bool>& pair)
{
    return pair.first;
}
inline bool insert_success(dim_t ident)
{
    return true;
}
inline dim_t insert_success(const std::pair<dim_t, bool>& pair)
{
    return pair.second;
}
template <class T>
void test_ContainerStoreMax(T* container, bool grant_unique = true)
{
    typedef std::basic_string<typename OP::trie::stem::StemStore::atom_t> tst_str_t;
    std::unordered_set<tst_str_t> random_control;
    auto rnd_str = [&random_control, container]()->tst_str_t {
        auto len = (std::rand() % 1000) + 1;
        tst_str_t res(len, 0);
        for (OP::trie::StemStore::atom_t start = 0;; ++start)
        {//force uniqueness loop
            std::iota(std::begin(res), std::end(res), start);
            std::random_shuffle(std::begin(res), std::end(res));
            if (random_control.insert(res.substr(0, container->height())).second)
                return res;
        }
    };

    typedef std::map<typename OP::trie::StemStore::dim_t, tst_str_t> swatch_t;
    swatch_t swatches;
    while (swatches.size() < container->width())
    {
        if (!swatches.empty() && (std::rand() & 1) == 1)//make random erase
        {
            auto it_to_erase = swatches.begin();
            //skip random index
            for (auto n = (std::rand() % swatches.size()); n; --n)
                ++it_to_erase;
            container->erase(it_to_erase->first);
            if (grant_unique)
                tresult.assert_true(container->length(it_to_erase->first) == 0, OP_CODE_DETAILS());
            swatches.erase(it_to_erase);;
            tresult.assert_true(swatches.size() == container->size(), OP_CODE_DETAILS());
            auto rnd = rnd_str();;
            //random string may be bigger than container can accomodate
            auto accomodate_size = std::min<OP::trie::StemStore::dim_t>(rnd.length(), container->height());
            auto first = std::begin(rnd);
            auto insert_result = container->accommodate(first, std::end(rnd));
            auto idx = index_of_accomodate(insert_result);
            if (insert_success(insert_result))
            {
                auto swatch_ins_res = swatches.emplace(idx, rnd);
                if (grant_unique)
                    tresult.assert_true(swatch_ins_res.second, OP_CODE_DETAILS()) //must be unique index of accommodate


                    if (rnd.length() < container->height())
                    {
                        tresult.assert_true(first == std::end(rnd), OP_CODE_DETAILS());
                        tresult.assert_true(container->length(idx) == rnd.length(), OP_CODE_DETAILS());
                    }
                    else
                    {
                        tresult.assert_true(*first == rnd[accomodate_size], OP_CODE_DETAILS());
                        tresult.assert_true(container->length(idx) == container->height(), OP_CODE_DETAILS());
                    }
            }
        }
    }
    //compare result sets
    for (auto kv : swatches)
    {
        auto pair_seq = container->chain_at(kv.first);;
        auto accomodate_size = std::min<OP::trie::StemStore::dim_t>(kv.second.length(), container->height());
        auto rnd_begin = std::begin(kv.second);
        tresult.assert_true(safe_equal(pair_seq.first, pair_seq.second, rnd_begin, rnd_begin + accomodate_size), OP_CODE_DETAILS());
    }
}
void test_StemStoreIndex(OP::utest::TestRuntime &tresult)
{
    //auto store = std::unique_ptr<OP::trie::StemStore>(OP::trie::StemStore::create(16, 16));
    //auto index = std::unique_ptr<OP::trie::StemStoreIndex>(
    //    OP::trie::StemStoreIndex::create(std::move(store)));
    //const OP::trie::StemStore::atom_t seq1[] = { 'c' };
    //auto b_seq1 = std::begin(seq1);
    //auto r1 = index->accommodate(b_seq1, std::end(seq1));
    //tresult.assert_true(r1.second, OP_CODE_DETAILS());
    //tresult.assert_true(r1.first == 0, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq1 == std::end(seq1), OP_CODE_DETAILS());
    //tresult.assert_true(index->size() == 1, OP_CODE_DETAILS());
    //const OP::trie::StemStore::atom_t seq2[] = { 'c', 'd' };
    //auto b_seq2 = std::begin(seq2);
    //auto r2 = index->accommodate(b_seq2, std::end(seq2));
    //tresult.assert_true(!r2.second, OP_CODE_DETAILS());
    //tresult.assert_true(r2.first == 0, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq2 == std::begin(seq2) + 1, OP_CODE_DETAILS());
    //tresult.assert_true(index->size() == 1, OP_CODE_DETAILS());
    //const OP::trie::StemStore::atom_t seq3[] = { 'a', 'd' };
    //auto b_seq3 = std::begin(seq3);
    //auto r3 = index->accommodate(b_seq3, std::end(seq3));
    //tresult.assert_true(r3.second, OP_CODE_DETAILS());
    //tresult.assert_true(r3.first == 0, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq3 == std::end(seq3), OP_CODE_DETAILS());
    //tresult.assert_true(index->size() == 2, OP_CODE_DETAILS());
    //const OP::trie::StemStore::atom_t seq4[] = { 'b', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    //auto b_seq4 = std::begin(seq4);
    //auto r4 = index->accommodate(b_seq4, std::end(seq4));
    //tresult.assert_true(r4.second, OP_CODE_DETAILS());
    //tresult.assert_true(r4.first == 1, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq4 == std::end(seq4) - 1, OP_CODE_DETAILS()) //'f' is out of range
    //    tresult.assert_true(index->size() == 3, OP_CODE_DETAILS());
    ////start it again
    //b_seq4 = std::begin(seq4);;
    //r4 = index->accommodate(b_seq4, std::end(seq4));
    //tresult.assert_true(!r4.second, OP_CODE_DETAILS());
    //tresult.assert_true(r4.first == 1, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq4 == std::end(seq4) - 1, OP_CODE_DETAILS()) //'f' is out of range
    //    tresult.assert_true(index->size() == 3, OP_CODE_DETAILS());
    ////test insertion at last position
    //const OP::trie::StemStore::atom_t seq5[] = { 'd', 'a' };
    //auto b_seq5 = std::begin(seq5);
    //auto r5 = index->accommodate(b_seq5, std::end(seq5));
    //tresult.assert_true(r5.second, OP_CODE_DETAILS());
    //tresult.assert_true(r5.first == 3, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq5 == std::end(seq5), OP_CODE_DETAILS());
    //tresult.assert_true(index->size() == 4, OP_CODE_DETAILS());
    ////check the rest of container
    //OP::trie::StemStore::atom_t seq6[] = { 'e', 'a', 'b' };
    //for (OP::trie::StemStore::atom_t i = 0; i < (16 - 4); ++i)
    //{
    //    seq6[0] = 'e' + i;
    //    auto b_seq6 = std::begin(seq6);
    //    auto r6 = index->accommodate(b_seq6, std::end(seq6));
    //    tresult.assert_true(r6.second, OP_CODE_DETAILS());
    //    tresult.assert_true(r6.first == index->size() - 1, OP_CODE_DETAILS());
    //    tresult.assert_true(b_seq6 == std::end(seq6), OP_CODE_DETAILS());
    //    tresult.assert_true(index->size() == 5 + i, OP_CODE_DETAILS());
    //}
    ////now test range case
    //OP::trie::StemStore::atom_t seq7[] = { 't', 'a', 'b' };
    //auto b_seq7 = std::begin(seq7);
    //auto r7 = index->accommodate(b_seq7, std::end(seq7));
    //tresult.assert_true(!r7.second, OP_CODE_DETAILS());
    //tresult.assert_true(r7.first == OP::trie::StemStore::nil_c, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq7 == std::begin(seq7), OP_CODE_DETAILS());
    //tresult.assert_true(index->size() == 16, OP_CODE_DETAILS());
    ////find&erase something

    //OP::trie::StemStore::atom_t seq8[] = { 'z', 'z', 'z' };
    //auto b_seq8 = std::begin(seq8);
    //tresult.assert_true(OP::trie::StemStore::nil_c == index->find_prefix(b_seq8, std::end(seq8)), OP_CODE_DETAILS());
    //tresult.assert_true(b_seq8 == std::begin(seq8), OP_CODE_DETAILS());
    //b_seq3 = std::begin(seq3);;
    //tresult.assert_true(0 == index->find_prefix(b_seq3, std::end(seq3)), OP_CODE_DETAILS());
    //tresult.assert_true(b_seq3 == std::end(seq3), OP_CODE_DETAILS());
    //index->erase(0);;//
    //tresult.assert_true(15 == index->size(), OP_CODE_DETAILS());
    //b_seq3 = std::begin(seq3);;
    //tresult.assert_true(OP::trie::StemStore::nil_c == index->find_prefix(b_seq3, std::end(seq3)), OP_CODE_DETAILS());
    //tresult.assert_true(b_seq3 == std::begin(seq3), OP_CODE_DETAILS());
    //b_seq7 = std::begin(seq7);;
    //r7 = index->accommodate(b_seq7, std::end(seq7));
    //tresult.assert_true(r7.second, OP_CODE_DETAILS());
    //tresult.assert_true(r7.first == 15, OP_CODE_DETAILS());
    //tresult.assert_true(b_seq7 == std::end(seq7), OP_CODE_DETAILS());
    //tresult.assert_true(index->size() == 16, OP_CODE_DETAILS());
    //b_seq7 = std::begin(seq7);;
    //tresult.assert_true(15 == index->find_prefix(b_seq7, std::end(seq7)), OP_CODE_DETAILS());
    //tresult.assert_true(b_seq7 == std::end(seq7), OP_CODE_DETAILS());
}

void test_StemStoreMax(OP::utest::TestRuntime &tresult)
{
    auto tst1 = std::unique_ptr<StemStore>(StemStore::create(256, 255));;
    for (auto i = 0; i < tst1->width(); ++i)
        tresult.assert_true(tst1->length(i) == 0, OP_CODE_DETAILS());
    test_ContainerStoreMax(tst1.get());;
}

static auto module_suite = OP::utest::default_test_suite("Stems")
.declare("max", test_StemStoreMax)
;