// ScowSAT

#ifndef SCOWSAT_THREAD_SAFE_QUEUE_H
#define SCOWSAT_THREAD_SAFE_QUEUE_H

#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

template <typename T>
struct thread_safe_queue {
	std::mutex queue_mutex;
	std::condition_variable cv;
	std::queue<T> contents;
	std::atomic<int> queue_length{0};
	std::atomic<int> total_puts{0};

	void put(T&& t) {
		total_puts++;
		{

			std::unique_lock<std::mutex> lk(queue_mutex);
			contents.push(std::move(t));
			queue_length++;
		}
		// XXX: Is it okay in the multi-producer context to have this outside of the locked region?
		cv.notify_one();
	}

	T get() {
		std::unique_lock<std::mutex> lk(queue_mutex);
		cv.wait(lk, [&]() { return not contents.empty(); });
		T value = contents.front();
		contents.pop();
		queue_length--;
		return value;
	}
};

#endif

