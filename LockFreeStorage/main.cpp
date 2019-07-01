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

	static_assert(STORAGE_COUNT != 0, "");

private:
	struct Node {
		TYPE value;
		Node* next;
	};

	union TagIndex {
		struct {
			uint32_t tag;
			uint32_t index;
		} ti;
		uint64_t u;

		static const uint64_t Inviled = UINT64_MAX;
	};

	Node* pool_;
	std::atomic<uint64_t> poolTop_;
	std::atomic<Node*> top_;

	std::atomic<uint32_t> poolTag_;
	std::atomic<uint32_t> topTag_;
	std::atomic<uint32_t> count_;

	Node* allocNode() {
		_ASSERT(poolTop_.load() != TagIndex::Inviled);
		TagIndex poolTop;
		poolTop.u = poolTop_.load();
		while (1) {
			uint64_t next = reinterpret_cast<TagIndex*>(pool_ + poolTop.ti.index)->u;
			if (poolTop_.compare_exchange_strong(poolTop.u, next)) {
				break;
			}
		}

		return pool_ + poolTop.ti.index;
	}

	void deallocNode(Node* node) {
		TagIndex next;
		next.ti.tag = poolTag_.fetch_add(1);
		next.ti.index = node - pool_;

		TagIndex poolTop;
		poolTop.u = poolTop_.load();
		while (1) {

			reinterpret_cast<TagIndex*>(node)->u = poolTop.u;
			if (poolTop_.compare_exchange_strong(poolTop.u, next.u)) {
				break;
			}
		}
	}

public:
	LFStack() {
		top_ = nullptr;
		pool_ = reinterpret_cast<Node*>(_aligned_malloc(sizeof(Node) * STORAGE_COUNT, alignof(Node)));
		poolTag_ = topTag_ = 0;

		// poolÇÃèâä˙âª
		TagIndex* node = reinterpret_cast<TagIndex*>(pool_);
		for (int i = 0; i < STORAGE_COUNT; i++) {
			node = reinterpret_cast<TagIndex*>(pool_ + i);
			node->ti.tag = poolTag_.fetch_add(1);
			node->ti.index = i + 1;
		}
		node->u = TagIndex::Inviled;

		TagIndex poolTop;
		poolTop.ti.tag = poolTag_.fetch_add(1);
		poolTop.ti.index = 0;
		poolTop_ = poolTop.u;

		//TagIndex top;
		//top.ti.tag = topTag_.fetch_add(1);
		//top.ti.index = 0;
		//top_ = top.u;

	}

	~LFStack() {
		_aligned_free(pool_);
	}

	void push(TYPE data) {
		auto top = top_.load();
		Node* node = allocNode();
		node->value = data;
		node->next  = top;
		while (1) {
			if (top_.compare_exchange_strong(node->next, node, std::memory_order_relaxed)) {
				count_++;
				break;
			}
		}

	}

	TYPE pop() {
		//_ASSERT(top_ != nullptr);

		auto top = top_.load();

		TYPE data;
		while (1) {
			data = top->value;
			if (top_.compare_exchange_strong(top, top->next, std::memory_order_relaxed)) {
				deallocNode(top);
				count_--;
				break;
			}
		}

		return data;
	}

	bool empty() {
		return count_.load() == 0;
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
	v[THREAD_COUNT * ADD_COUNT] = 0;

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

	for (auto d : v) {
		_ASSERT(d == 0);
	}


	return 0;
}

