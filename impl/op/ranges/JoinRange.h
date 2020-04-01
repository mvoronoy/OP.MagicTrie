#ifndef _OP_RANGES_JOIN_RANGE__H_
#define _OP_RANGES_JOIN_RANGE__H_
#include <iterator>
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>

#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

namespace OP
{
    namespace ranges
    {
        template <class OwnerRange, class LeftIterator, class RightIterator>
        struct JoinRangeIterator;

        template <class SourceRange1, class SourceRange2>
        struct JoinRange : 
            public OrderedRange< IteratorWrap<typename SourceRange1::iterator> >
        {
            using this_t = JoinRange<SourceRange1, SourceRange2> ;
            using iterator = IteratorWrap<typename SourceRange1::iterator>;
            using base_t = OrderedRange<iterator>;

            static_assert(SourceRange1::is_ordered_c, "Source range(1) must support ordering");
            static_assert(SourceRange2::is_ordered_c, "Source range(2) must support ordering");
            
            using left_iterator = typename SourceRange1::iterator ;
            using right_iterator = typename SourceRange2::iterator ;

            
            using left_right_comparator_t =
                std::function<int(const typename SourceRange1::key_type&, const typename SourceRange2::key_type&)>;
            /**
            * @param iterator_comparator - binary predicate `int(const iterator&, const iterator&)` that implements tripple compare of current 
            *    iterator positions (< 0, == 0, > 0)
            */
            JoinRange(
                std::shared_ptr<SourceRange1 const> r1, 
                std::shared_ptr<SourceRange2 const> r2,
                typename base_t::key_comparator_t comparator) noexcept
                : base_t(std::forward<key_comparator_t>(comparator))
                , _left(r1)
                , _right(r2)
                
            {
            }
            JoinRange() = delete;
            iterator begin() const override
            {
                auto zis = std::static_pointer_cast<this_t const> (shared_from_this());
                iterator result(_left->begin(),
                    make_shared_void(new IteratorPayload{
                        zis,
                        //_left->begin(), 
                        _right->begin()
                    })
                );
                
                seek(result);
                return result;
            }
            bool in_range(const iterator& check) const override
            {
                return _left->in_range(check) && _right->in_range(payload(check)->_right);
            }
            void next(iterator& pos) const override
            {
                _left->next(pos);
                auto pload = payload(pos);

                if (pload->_optimize_right_forward)
                {
                    _right->next(pload->_right);
                }
                seek(pos);
            }
            iterator lower_bound(const typename base_t::key_type& key) const override
            {
                auto zis = std::static_pointer_cast<this_t const> (shared_from_this());
                iterator result(_left->lower_bound(key),
                    make_shared_void(new IteratorPayload(zis, _right->lower_bound(key) ))
                    );
                seek(result);
                return result;
            }

        private:
            struct IteratorPayload
            {
                IteratorPayload(std::shared_ptr< this_t const > owner_range, right_iterator right)
                    : _owner_range(owner_range)
                    , _right(right)
                    , _optimize_right_forward(false)
                {}
                std::shared_ptr< this_t const > _owner_range;
                right_iterator _right;
                /**Very special case when right == left, then ::next must be called for both iterators (not only for left)*/
                bool _optimize_right_forward;
            };
            static IteratorPayload* payload(iterator& i)
            {
                return reinterpret_cast<IteratorPayload*>(i.payload().get());
            }
            static const IteratorPayload* payload(const iterator& i)
            {
                return reinterpret_cast<IteratorPayload*>(i.payload().get());
            }
            void seek(iterator &pos) const
            {
                auto pload = payload(pos);
                pload->_optimize_right_forward = false;
                while (in_range(pos))
                {
                    auto diff = key_comp()(
                        key_discovery::key(pos), key_discovery::key(pload->_right));
                    if (diff < 0) 
                    {
                        _left->next(pos);
                    }
                    else {
                        if (diff == 0) 
                        {
                            pload->_optimize_right_forward = true;
                            return;
                        }
                        _right->next(pload->_right);
                    }
                }

            }
            std::shared_ptr<SourceRange1 const> _left;
            std::shared_ptr<SourceRange2 const> _right;
        };
        /*
        template <class OwnerRange, class LeftIterator, class RightIterator>
        struct JoinRangeIterator
        {
            typedef JoinRangeIterator<OwnerRange, LeftIterator, RightIterator> this_t;
            //typedef typename OwnerRange::left_iterator::value_type value_type;
            typedef LeftIterator left_iterator;
            typedef RightIterator right_iterator;
            typedef std::remove_reference_t< decltype(key_discovery::key(std::declval<left_iterator>())) > key_type;

            friend OwnerRange;
            JoinRangeIterator(std::shared_ptr< OwnerRange const> owner_range,
                left_iterator && left,
                right_iterator && right) noexcept
                : _owner_range(owner_range)
                , _left(std::move(left))
                , _right(std::move(right))
                , _optimize_right_forward(false)
            {}
            this_t& operator ++()
            {
                _owner_range->next(*this);
                return *this;
            }
            this_t operator ++(int)
            {
                this_t result = *this;
                _owner_range->next(*this);
                return result;
            }
            auto operator* () -> decltype(*std::declval<left_iterator>()) const
            {
                return *left();
            }
            const key_type& key() const
            {
                return _left.key();
            }

        private:
            const left_iterator& left() const
            {
                return _left;
            }
            const right_iterator& right() const
            {
                return _right;
            }
            std::shared_ptr< OwnerRange const > _owner_range;
            left_iterator _left;
            right_iterator _right;
            //Very special case when right == left, then ::next must be called for both iterators (not only for left)
            bool _optimize_right_forward;
        };*/

    } //ns: ranges
} //ns: OP
#endif //_OP_RANGES_JOIN_RANGE__H_
