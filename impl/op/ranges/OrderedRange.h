#pragma once
#include <op/ranges/PrefixRange.h>
#include <memory>

#if defined(_MSC_VER)
//#pragma warning(push)
#pragma warning(disable: 4503 )
#endif

namespace OP {
    namespace ranges {

        namespace key_discovery {

            /**
            *   Specialization of discovery key from iterators for types that support dereferncing of "first". For example it 
            *   may be iterators produced by std::map
            */
            template <class I>
            auto key(const I& i) -> decltype(i->first)
            {
                return i->first;
            }
            template <class I>
            auto value(const I& i) -> decltype(i->second)
            {
                return i->second;
            }
            
            
            /**
            *   Specialization of discovery key from iterators for types that may return value by "key()" method. For example it
            *   may be iterators produced by other OrderedRange
            */
            template <class I>
            auto key(const I& i) -> decltype(i.key())
            {
                return i.key();
            }

            template <class I>
            auto value(const I& i) -> decltype(i.key(), *i )
            {
                return *i;
            }
        } //ns:details


        template <class Iterator>
        struct OrderedRange : public PrefixRange<Iterator, true>
        {
            using iterator = Iterator;
            
            using this_t = OrderedRange<iterator>;
            using key_type = decltype(OP::ranges::key_discovery::key(std::declval<const iterator&>()));
            using key_t = key_type;

            template <class OtherRange>
            using join_comparator_t =
                std::function<int(const key_type&, const typename OtherRange::key_type&)>;
            //typename join_range_t < OtherRange> ::iterator_comparator_t;

            using key_comparator_t =
                std::function<int(const key_type&, const key_type&)>;
            
            using join_iterator_t = IteratorWrap<iterator>;
            using join_range_t = OrderedRange< join_iterator_t > ;

            OrderedRange() = default;

            OrderedRange(key_comparator_t key_cmp)
                : _key_cmp(std::forward<key_comparator_t>(key_cmp))
            {}


            template <class OtherRange>
            inline std::shared_ptr< join_range_t > join(std::shared_ptr< OtherRange const > range,
                join_comparator_t<OtherRange> cmp) const;

            template <class OtherRange>
            inline std::shared_ptr< join_range_t > join(std::shared_ptr< OtherRange > range,
                join_comparator_t<OtherRange> cmp) const
            {
                std::shared_ptr< OtherRange const > cast{ range };
                return join(cast, cmp);
            }

            template <class OtherRange>
            inline std::shared_ptr< join_range_t > join(std::shared_ptr< OtherRange const > range) const
            {
                return this->join(range, [this](auto const& left, auto const& right)->int {
                    return key_comp()(left, right);
                });
            }

            const key_comparator_t& key_comp() const
            {
                return _key_cmp;
            }
            virtual iterator lower_bound(const key_type& key) const = 0;
        
        private:
            key_comparator_t _key_cmp;
        };


        template <class SourceRange>
        struct OrderedFilteredRange : 
            public FilteredRangeBase<SourceRange, OrderedRange< typename SourceRange::iterator > >
        {
            using base_t = FilteredRangeBase<SourceRange, OrderedRange< typename SourceRange::iterator > >;

            using base_t::base_t;

            iterator lower_bound(const typename base_t::key_type& key) const override
            {
                auto lower = static_cast<const base_t&>(*source_range()).lower_bound(key);
                seek(lower);
                return lower;
            }
        };

    }//ns:ranges
}//ns:OP
#include <op/ranges/JoinRange.h>
namespace OP {
    namespace ranges {
       
        template <class Iterator>
        template <class OtherRange>
        inline std::shared_ptr< OrderedRange<IteratorWrap<Iterator>> > OrderedRange<Iterator>::join(std::shared_ptr<OtherRange const> range,
            join_comparator_t<OtherRange> cmp) const
        {
            std::shared_ptr<OrderedRange<Iterator> const> the_ptr( std::static_pointer_cast<OrderedRange<Iterator> const> (shared_from_this()) );
            using range_impl_t = JoinRange<this_t, OtherRange>;
            auto r = new range_impl_t(the_ptr, range, std::forward<join_comparator_t<OtherRange>>(cmp));
            return std::shared_ptr<join_range_t>(r);
        }

    }
}
#ifdef _MSC_VER
//#pragma warning( pop )
#endif /*_MSC_VER*/
