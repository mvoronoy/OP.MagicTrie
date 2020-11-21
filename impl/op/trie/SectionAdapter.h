#pragma once
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <memory>
#include <iterator>
#include <string>
#include <op/common/typedefs.h>

namespace OP
{
    namespace trie
    {
        using namespace OP::ranges;
        namespace details
        {
            template <class SourceRange>
            struct SectionRangeTraits
            {
                using source_range_t = SourceRange;
                using source_iterator_t = typename source_range_t::iterator;
                using key_type = typename source_iterator_t::key_t;
                using key_t = key_type;
                using atom_string_t = key_type;

                using value_t = typename source_iterator_t::value_type;

                /** function that returns string truncation result*/
                using strain_fnc_t = std::function<const key_t& (const source_iterator_t&)>;
                using key_eval_policy_t = FunctionResultCachedPolicy<source_iterator_t, strain_fnc_t>;
            };
        } //ns::details
        
        template <class SourceIterator>
        using string_map_fnc_t = std::function< OP::trie::atom_string_t(const SourceIterator&) >;
        /**
        *   Special kind of OrderedRange that utilize feature of Trie where all entrie below prefix are lexicographicaly ordered.
        * This range provide access to ordered sequence of suffixes. In simplified view you can think about it as cut of trie entry:
        * \code
        *   trie
        *       ->prefixed_range( prefix )
        *       ->map([&](const auto& i){
        *           return key_discovery::key(i).substr( prefix.length() ); //cut string after the prefix
        *       });
        * \endcode
        * If optional parameter trunc_func specified, `substr` also restricted by length
        * \code
        *  auto str = key_discovery::key(i).substr( prefix.length() );
        *  str = str.substr(0, _trunc_fun(str));
        * \endcode
        * In compare with code above this Range returns ordered sequence
        */
        template <class SourceRange>
        struct TrieSectionAdapter : public
            FunctionalRange<
                SourceRange, 
                typename SourceRange::key_t,
                FunctionResultCachedPolicy<typename SourceRange::iterator, typename SourceRange::key_t >,
                OrderedRange< typename SourceRange::key_t, typename SourceRange::value_t >
            >
        {
            static_assert(SourceRange::is_ordered_c, "Source range must support ordering");
            using this_t = TrieSectionAdapter<SourceRange>;
            using iterator_map_fnc_t = string_map_fnc_t<typename SourceRange::iterator>;

            using filter_base_t = FunctionalRange<
                SourceRange,
                typename SourceRange::key_t,
                FunctionResultCachedPolicy<typename SourceRange::iterator, typename SourceRange::key_t >,
                OrderedRange< typename SourceRange::key_t, typename SourceRange::value_t >
            >;
            using base_t = filter_base_t;

            using traits_t = details::SectionRangeTraits<SourceRange>;
            using source_range_t = typename traits_t::source_range_t;
            using source_iterator_t = typename traits_t::source_iterator_t;

            using key_eval_policy_t = typename filter_base_t::key_eval_policy_t;
            using trunc_fnc_t = std::function<size_t(const atom_string_t&)>;

            using section_iterator_t = FunctionalRangeIterator<source_iterator_t, this_t, key_eval_policy_t>;
            using atom_string_t = typename traits_t::atom_string_t;
            
            using iterator = typename base_t::iterator;
            using key_t = typename base_t::key_t;
            using value_t = typename base_t::value_t;

            TrieSectionAdapter(
                std::shared_ptr<const source_range_t> source_range, 
                atom_string_t prefix,
                trunc_fnc_t trunc_fnc = nullptr
                ) noexcept
                : filter_base_t (
                    std::move(source_range), 
                    trunc_fnc 
                        ? iterator_map_fnc_t([this](const source_iterator_t& i) {return trunc_transform(i);})
                        : iterator_map_fnc_t([this](const source_iterator_t& i) {return simple_transform(i);}),
                    &this_t::key_compare
                )
                , _prefix(std::move(prefix))
                , _trunc_fnc(std::move(trunc_fnc))
            {
            }

            iterator begin() const override
            {
                auto res = filter_base_t::begin();
                if(!res)
                    return res;
                auto &functional_iter = res.OP_TEMPL_METH(impl)<typename base_t::iterator_impl>();
                if( skip_first_if_empty(functional_iter.source())
                    && this->source_range()->in_range(functional_iter.source()) )
                { //notify cache about changes
                    functional_iter.key_eval_policy().on_after_change(functional_iter.source());
                }    
                return res;
            }
            iterator lower_bound(const typename iterator::key_t& key) const override
            {
                auto res = this->source_range()->lower_bound(_prefix + key);
                skip_first_if_empty(res);
                if (this->source_range()->in_range(res))
                {
                    auto policy_copy = this->key_eval_policy(); //clone
                    policy_copy.on_after_change(res); //notify local policy copy that key was changed
                    return iterator(
                        std::const_pointer_cast<typename base_t::range_t const>(this->shared_from_this()),
                        std::unique_ptr<typename iterator::RangeIteratorImpl>(
                            new iterator_impl(std::move(res), std::move(policy_copy))));
                }
                return end();
            }
        private:
            static int key_compare(const atom_string_t& left, const atom_string_t& right)
            {
                return left.compare(right);
            }
            atom_string_t simple_transform(const source_iterator_t& i) const
            {
                return i.key().substr(_prefix.length());
            }
            atom_string_t trunc_transform(const source_iterator_t& i) const
            {
                auto str =
                    i.key().substr(_prefix.length());

                str.resize(_trunc_fnc(str));
                return str;
            }
            /**When prefixed_range starts iteration it may point to terminal string, 
            * so to avoid empty string in result need check and skip
            * @return true, if row was skipped, false - no rows skipped
            */
            bool skip_first_if_empty(source_iterator_t& from) const
            {
                if (this->source_range()->in_range(from) )
                {
                    if (from.key().length() == _prefix.length())
                    {
                        this->source_range()->next(from);
                        return true;
                    }
                }
                return false;
            }
            atom_string_t _prefix;
            trunc_fnc_t _trunc_fnc;
        };
    } //ns:trie
}//ns:OP
