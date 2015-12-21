#ifndef _OP_SERVICE_BACKGROUNDWORKER__H_
#define _OP_SERVICE_BACKGROUNDWORKER__H_
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>

namespace OP
{
	namespace service 
	{
		class BackgroundWorker
		{
		public:
			typedef std::function<void(void)> task_t;
			virtual ~BackgroundWorker()
			{
				stop_all();
			}
			void push(task_t work)
			{
				if (_busy_count.load() < _limit_count)
				{
					guard_t poolg(_pool_lock);
					if (_pool.size() <= _busy_count.load() && _pool.size() < _limit_count) //can alloc
					{
						_pool.emplace_back(
							std::bind(std::mem_fn(&BackgroundWorker::thread_task), this, std::move(work))
							);
						return;
					}
				}
				push_queue(std::move(work));
			}
		private:
			typedef std::vector<std::thread> thread_pool_t;
			
			typedef std::deque<task_t> queue_t;
			typedef std::mutex lock_t;
			typedef std::unique_lock<lock_t> guard_t;
			typedef std::condition_variable condition_var_t;
			typedef std::atomic<std::uint32_t> atomic_uint_t;
			queue_t _task_queue;
			lock_t _queue_lock, _pool_lock;
			condition_var_t _queue_condition;
			atomic_uint_t _busy_count;
			const std::uint32_t _limit_count;
			thread_pool_t _pool;
			std::atomic<int> _exit_mode;
		protected:
			BackgroundWorker(std::uint32_t limit = 1, std::uint32_t initial = 1)
				: _limit_count(limit)
				, _exit_mode(0)
				, _busy_count(0)
			{
				for (;initial; --initial)
				{
					_pool.emplace_back(
						std::bind(std::mem_fn(&BackgroundWorker::thread_task0), this)
						);
				}
			}
			void stop_all()
			{
				++_exit_mode;
				task_t exit = []() {};
				guard_t poolg(_pool_lock);
                for (size_t i = _pool.size(); i; --i)
                {
                    push_queue(exit);
                }
                for (auto& t : _pool)
                {
                    t.join();
                }
			}
			void thread_task0()
			{
				//std::cout << "Hello from thread\n";
				thread_task(std::move(wait_and_pop()));
			}
			/**
			*	First handle default task, then wait from queue
			*/
			void thread_task(task_t def_task)
			{
				//std::cout << "Hello from thread (dd)\n";
				while (0 == _exit_mode.load())
				{
					++_busy_count;
					def_task();
					--_busy_count;
					if (0 == _exit_mode.load())
					{//don't enter wait if exit_mode already on
						def_task = std::move(wait_and_pop());
					}
				} 
					
			}

			task_t wait_and_pop()
			{
				guard_t g(_queue_lock);
				while (_task_queue.empty())
				{
					_queue_condition.wait(g);
				}
				auto result = std::move(_task_queue.front());
				_task_queue.pop_front();
				return result;
			}
			void push_queue(task_t work)
			{
				guard_t g(_queue_lock);
				_task_queue.emplace_back(std::move(work));
				g.unlock();
				_queue_condition.notify_one();
			}
		};
	}//ns:service
}//ns:OP
#endif //_OP_SERVICE_BACKGROUNDWORKER__H_
