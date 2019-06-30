#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <stack>
#include <Windows.h>
#include <sysinfoapi.h>

double GetTimeFromStartEnd(const LARGE_INTEGER& start, const LARGE_INTEGER& end)
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
}

template<typename TYPE, uint64_t STORAGE_COUNT>
class LFStack {

private:
	struct Node {
		TYPE value;
		Node* next;
	};

	Node* pool_;
	std::atomic<Node*> top_;

public:
	LFStack() {
		top_ = nullptr;
	}

	~LFStack() {
	}

	void push(TYPE data) {
		auto top = top_.load();
		Node* node = new Node;
		node->value = data;
		while (1) {
			node->next = top;
			if (top_.compare_exchange_strong(top, node, std::memory_order_relaxed)) {
				break;
			}
		}

	}

	TYPE pop() {
		_ASSERT(top_ != nullptr);

		auto top = top_.load();

		TYPE data;
		while (1) {
			data = top->value;
			if (top_.compare_exchange_strong(top, top->next, std::memory_order_relaxed)) {
				delete top;
				break;
			}
		}

		return data;
	}

	bool empty() {
		return top_.load() == nullptr;
	}
};

int main() {
	constexpr int THREAD_COUNT = 1;
	constexpr int ADD_COUNT = 1000000;
	LFStack<int, THREAD_COUNT * ADD_COUNT> s;
	LARGE_INTEGER start, end;

	std::mutex mutex;
	std::vector<int> v;
	v.resize(THREAD_COUNT * ADD_COUNT + 1);
	for (int i = 0; i < THREAD_COUNT * ADD_COUNT + 1; i++) {
		v[i] = i;
	}

	QueryPerformanceCounter(&start);
	std::thread threads[THREAD_COUNT];
	std::atomic<uint32_t> num = 0;
	for (auto& t : threads) {
		t = std::thread([&]() {
			for (int i = 0; i < ADD_COUNT; i++) {
				s.push(num++);
			}
		});
	}

	std::thread popThread[THREAD_COUNT];

	for (auto& t : popThread) {
		t = std::thread([&]() {
			for (int i = 0; i < ADD_COUNT; i++) {
				while (s.empty()) {}
				auto d = s.pop();
				mutex.lock();
				_ASSERT(v[d] == d);
				v[d] = 0;
				mutex.unlock();
			}
		});
	}

	for (auto& t : threads) {
		t.join();
	}
	for (auto& t : popThread) {
		t.join();
	}
	QueryPerformanceCounter(&end);
	printf("lock free stack time:%lf[ms] - End\n", GetTimeFromStartEnd(start, end));

	int i = 0;
	while (!s.empty()) {
		auto d = s.pop();
		_ASSERT(v[d] == d);
		v[d] = 0;
		i++;
	}

	return 0;
}

