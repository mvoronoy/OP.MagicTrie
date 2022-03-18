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
        *   Ordered range that allows iteration until iterator position meets logical criteria
        */
        template <class TTrie>
        using TakewhileSequenceFactory = TrieSequenceFactory<TTrie, typename Ingredient<TTrie>::TakeWhileInRange>;

        template <class TTrie>
        using PrefixSequenceFactory = TrieSequenceFactory<TTrie, 
            typename Ingredient<TTrie>::PrefixedBegin, 
            typename Ingredient<TTrie>::PrefixedLowerBound,
            typename Ingredient<TTrie>::PrefixedInRange >;
        
        /**Used to iterate over immediate children of parent iterator*/
        template <class TTrie>
        using ChildSequenceFactory = TrieSequenceFactory<TTrie, 
            typename Ingredient<TTrie>::ChildBegin, typename Ingredient<TTrie>::ChildInRange >;


        /**Used to iterate over keys situated on the same trie level*/
        template <class TTrie>
        using SiblingRangeAdapter = TrieSequenceFactory<TTrie,
            typename Ingredient<TTrie>::Find, typename Ingredient<TTrie>::SiblingNext>;

        ///**
        //*  Trie specific operation: create range that contains entries of 'from' that are started 
        //* with prefixes enumerated in param 'prefixes'
        //*/
        //template <class R1, class R2>
        //auto prefixes_continuation_range(R1&& from, R2&& prefixes)
        //{
        //    return from >> OP::flur::then::join(std::move(prefixes), [](const auto& left, const auto& right){
        //            //'right' key is taken from prefixes array so assuming (no assert needed) it shorter
        //            int cmp = left.key().compare(0, right.size(), right, 0, right.size());
        //            return cmp;
        //        });
        //}
    } //ns:trie
}//ns:OP
