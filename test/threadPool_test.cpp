#include <benchmark/benchmark.h>
#include <string>
#include <chrono>
#include "../code/pool/threadpool.h" // 包含你的线程池头文件

// #define TaskType 

// 定义一个任务，这个任务可以是线程池需要执行的具体工作
void CaculTask() {
    // 这里可以放入具体的任务逻辑
    // 例如，模拟一个简单的计算任务
    volatile int result = 0;
    for (int i = 0; i < 1000; ++i) {
        result += i;
    }
}

// 模拟IO任务
void IOTask() {
    // 模拟IO操作，休眠一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// 函数模板
template<typename T>
void ThreadPollTest(int nums, T&& task, benchmark::State& state, int tasksNum = 100) {
    // 初始化线程池，使用默认线程数量（8）
    ThreadPool pool(nums);

    for (auto _ : state) {
        for(int i = 0; i < tasksNum; i++) {
            // 在测试用例中提交任务到线程池
            pool.AddTask(std::forward<T>(task)); // MyTask 是你要执行的任务
        }
        pool.WaitForCompletion();
    }
}


#ifdef TaskType
static void BM_IO_ThreadPoll(benchmark::State& state) {
    ThreadPollTest(1, IOTask, state);
}
// Register the function as a benchmark
BENCHMARK(BM_IO_ThreadPoll);

static void BM_IO_ThreadPoll2(benchmark::State& state) {
    ThreadPollTest(2, IOTask, state);
}
BENCHMARK(BM_IO_ThreadPoll2);

static void BM_IO_ThreadPoll3(benchmark::State& state) {
    ThreadPollTest(3, IOTask, state);
}
// BENCHMARK(BM_IO_ThreadPoll3);

static void BM_IO_ThreadPoll4(benchmark::State& state) {
    ThreadPollTest(4, IOTask, state);
}
// BENCHMARK(BM_IO_ThreadPoll4);

static void BM_IO_ThreadPoll8(benchmark::State& state) {
    ThreadPollTest(8, IOTask, state);
}
// BENCHMARK(BM_IO_ThreadPoll8);

#else
static void BM_CPU_ThreadPoll(benchmark::State& state) {
    ThreadPollTest(1, CaculTask, state);
}
// Register the function as a benchmark
BENCHMARK(BM_CPU_ThreadPoll);

static void BM_CPU_ThreadPoll2(benchmark::State& state) {
    ThreadPollTest(2, CaculTask, state);
}
BENCHMARK(BM_CPU_ThreadPoll2);

static void BM_CPU_ThreadPoll3(benchmark::State& state) {
    ThreadPollTest(3, CaculTask, state);
}
BENCHMARK(BM_CPU_ThreadPoll3);

static void BM_CPU_ThreadPoll4(benchmark::State& state) {
    ThreadPollTest(4, CaculTask, state);
}
// BENCHMARK(BM_CPU_ThreadPoll4);

static void BM_CPU_ThreadPoll8(benchmark::State& state) {
    ThreadPollTest(8, CaculTask, state);
}
// BENCHMARK(BM_CPU_ThreadPoll8);

#endif

BENCHMARK_MAIN();