#include <benchmark/benchmark.h>

#include <vector>

// Функция, которую мы будем тестировать
void BM_VectorPushBack(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<int> v;
    for (int i = 0; i < state.range(0); ++i) {
      v.push_back(i);
    }
  }
}

// Регистрируем бенчмарк
BENCHMARK(BM_VectorPushBack)->Range(8, 8 << 10);

// Основная функция для запуска бенчмарков
BENCHMARK_MAIN();
