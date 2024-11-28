#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/trie/StemContainer.h>
#include <op/common/typedefs.h>

using namespace OP::trie::stem;
using namespace OP::trie;


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

void test_StemStoreMax(OP::utest::TestRuntime &tresult)
{
    auto tst1 = std::unique_ptr<StemStore>(StemStore::create(256, 255));;
    for (auto i = 0; i < tst1->width(); ++i)
        tresult.assert_true(tst1->length(i) == 0, OP_CODE_DETAILS());
    test_ContainerStoreMax(tst1.get());;
}

static auto module_suite = OP::utest::default_test_suite("vtm.Stems")
    .declare("max", test_StemStoreMax)
;