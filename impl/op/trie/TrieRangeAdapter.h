#pragma once
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <op/trie/MixedAdapter.h>
#include <memory>
#include <functional>

namespace OP
{
    namespace trie
    {

        /**
        *   Allows trie to mimic OP::ranges::PrefixRange capabilities
        */
        template <class TTrie>
        using TrieRangeAdapter = MixAlgorithmRangeAdapter<TTrie /*no other ingredients for default impl.*/>;
        
        /**
        *   Ordered range that allows iteration until iterator position meets logical criteria
        */
        template <class TTrie>
        using TakewhileTrieRangeAdapter = MixAlgorithmRangeAdapter<TTrie, typename Ingredient<TTrie>::TakeWhileInRange>;

        template <class TTrie>
        using PrefixSubrangeAdapter = MixAlgorithmRangeAdapter<TTrie, 
            typename Ingredient<TTrie>::PrefixedBegin, 
            typename Ingredient<TTrie>::PrefixedLowerBound,
            typename Ingredient<TTrie>::PrefixedInRange >;
        
        /**Used to iterate over immediate children of parent iterator*/
        template <class TTrie>
        using ChildRangeAdapter = MixAlgorithmRangeAdapter<TTrie, 
            typename Ingredient<TTrie>::ChildBegin, typename Ingredient<TTrie>::ChildInRange >;


        /**Used to iterate over keys situated on the same trie level*/
        template <class TTrie>
        using SiblingRangeAdapter = MixAlgorithmRangeAdapter<TTrie,
            typename Ingredient<TTrie>::Find, typename Ingredient<TTrie>::SiblingNext>;

        /**
        *  Trie specific operation: create range that contains entries of 'from' that are started 
        * with prefixes enumerated in param 'prefixes'
        */
        template <class RangeOfTrie>
        std::shared_ptr<RangeOfTrie const> prefixes_continuation_range(const std::shared_ptr<RangeOfTrie const>& from, std::shared_ptr<RangeOfTrie const> prefixes) 
        {
            static_assert(RangeOfTrie::is_ordered_c, "Expected ordered range for parameters 'from' and 'prefixes'");
            return from->if_exists(std::move(prefixes), [](const auto& left, const auto& right){
                    //'right' key is taken from prefixes array so assuming (no assert needed) it shorter
                    int cmp = left.compare(0, right.size(), right, 0, right.size());
                    return cmp;
                });
        }


    } //ns:trie
}//ns:OP
