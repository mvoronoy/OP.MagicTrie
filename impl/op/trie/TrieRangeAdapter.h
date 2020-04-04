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
            typename Ingredient<TTrie>::PrefixedBegin, typename Ingredient<TTrie>::PrefixedInRange >;
        
        /**Used to iterate over immediate children of parent iterator*/
        template <class TTrie>
        using ChildRangeAdapter = MixAlgorithmRangeAdapter<TTrie, 
            typename Ingredient<TTrie>::ChildBegin, typename Ingredient<TTrie>::ChildInRange >;


        /**Used to iterate over keys situated on the same trie level*/
        template <class TTrie>
        using SiblingRangeAdapter = MixAlgorithmRangeAdapter<TTrie,
            typename Ingredient<TTrie>::Find, typename Ingredient<TTrie>::SiblingNext>;
    } //ns:trie
}//ns:OP
