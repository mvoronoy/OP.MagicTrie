#pragma once
#include <op/trie/SegmentManager.h>
#include <op/trie/Transactional.h>
#include <op/trie/Exceptions.h>
#include <unordered_map>
#include <memory>
#include <deque>

namespace OP
{
    namespace trie{
        class TransactedSegmentManager :
            public SegmentManager
        {
            friend struct SegmentManager;
        public:

            ~TransactedSegmentManager()
            {
            }
            void begin_transaction()
            {
                ++_transaction_nesting;
            }
            void commit()
            {
                if (!_transaction_nesting)
                    throw OP::trie::TransactionIsNotStarted();
                if (!--_transaction_nesting)
                    return;
                _pos_cache.clear();
                while(!_operation_queue.empty())
                {
                    auto op = _operation_queue.front();
                    op->commit(*this);
                    _operation_queue.pop_front();
                }
            }
            void rollback()
            {
                if (!_transaction_nesting)
                    throw OP::trie::TransactionIsNotStarted();
                _pos_cache.clear();
                while(!_operation_queue.empty())
                {
                    auto op = _operation_queue.front();
                    op->rollback(*this);
                    _operation_queue.pop_front();
                }
                --_transaction_nesting;

            }
            //far_pos_t allocate(segment_idx_t segment_idx, segment_pos_t size) override
            //{
            //    if (!_transaction_nesting)
            //        throw OP::trie::TransactionIsNotStarted();

            //    auto far_pos = SegmentManager::allocate(segment_idx, size);
            //    auto roll_command = std::make_shared<AllocateMemoryOperation>(
            //        far_pos, size
            //        );
            //    _pos_cache.emplace(pos_cache_t::value_type(far_pos, roll_command));
            //    _operation_queue.emplace_back(std::move(roll_command));
            //    //mirror memory of the same size
            //    return far_pos;
            //}

            //void deallocate(far_pos_t addr) override
            //{
            //    if (!_transaction_nesting)
            //        throw OP::trie::TransactionIsNotStarted();
            //    //segment_idx_t segment_idx, segment_idx_t memblock_offset
            //    //don't really deallocate, just mark it for late
            //    auto roll_command = std::make_shared<DeallocateMemoryOperation>(addr);
            //    _operation_queue.emplace_back(std::move(roll_command));
            //}

        protected:
            TransactedSegmentManager(const char * file_name, bool create_new, bool readonly) :
                SegmentManager(file_name, create_new, readonly)
            {
                _transaction_nesting = 0;
            }
            virtual inline const uint8_t* const_from_offset(segment_idx_t segment_idx, segment_pos_t offset) const
            {
                far_pos_t pos = _far(segment_idx, offset);
                auto found = _pos_cache.find(pos);
                if (found == _pos_cache.end()) //may be find by non-transactional operation
                {
                    //no such block available for write, so return from parent
                    throw std::runtime_error("Trying modify non-transacted block");
                }
                return static_cast<AllocateMemoryOperation*>(found->second.get())->mirror;
            }
            virtual inline const uint8_t* updateable_from_offset(segment_idx_t segment_idx, segment_pos_t offset) const
            {
                far_pos_t pos = _far(segment_idx, offset);
                auto found = _pos_cache.find(pos);
                if (found == _pos_cache.end()) //may be find by non-transactional operation
                {
                    //mem position is not real rollback command
                    throw std::runtime_error("Trying modify non-transacted block");
                }
                return static_cast<AllocateMemoryOperation*>(found->second.get())->mirror;
            }
            virtual void begin_write_operation()
            {
                begin_transaction();
            }
            virtual void end_write_operation()
            {
                commit();
            }

        private:
            struct MemoryOperation
            {
                virtual ~MemoryOperation() = default;
                virtual void commit(TransactedSegmentManager& manager) = 0;
                virtual void rollback(TransactedSegmentManager& manager) = 0;
            };
            typedef std::shared_ptr<MemoryOperation> mem_operation_ptr_t;
            typedef std::deque<mem_operation_ptr_t> operation_queue_t;
            typedef std::unordered_map<far_pos_t, mem_operation_ptr_t> pos_cache_t;

            operation_queue_t _operation_queue;
            pos_cache_t _pos_cache;
            unsigned _transaction_nesting;
            struct AllocateMemoryOperation : public MemoryOperation
            {
                /**segment+offset of memory created in virtual memory*/
                far_pos_t vm_addr;
                /**transaction mirrored memory*/
                std::uint8_t* mirror;
                /**byte size of allocated block*/
                segment_pos_t size;

                AllocateMemoryOperation(far_pos_t a_vm_addr, segment_pos_t a_size) :
                    vm_addr(a_vm_addr),
                    size(a_size)
                {
                    mirror = new std::uint8_t[size];
                }
                ~AllocateMemoryOperation()
                {
                    free();
                }
                virtual void commit(TransactedSegmentManager& manager)
                {
                    free();
                }
                virtual void rollback(TransactedSegmentManager& manager)
                {
                    free();
                }
            private:
                void free()
                {
                    if (mirror)
                    {
                        delete[] mirror;
                        mirror = nullptr;
                    }
                }
            };
            struct DeallocateMemoryOperation : public MemoryOperation
            {

                far_pos_t vm_addr;
                DeallocateMemoryOperation(far_pos_t a_vm_addr) :
                    vm_addr(a_vm_addr)
                {

                }
                virtual void commit(TransactedSegmentManager& manager)
                {
                    
                }
                virtual void rollback(TransactedSegmentManager& manager)
                {
                    //do nothing
                }
            };

        };
    }
} //end of namespace OP
