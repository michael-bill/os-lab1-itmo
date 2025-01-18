#include <benchmark/benchmark.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <queue>
#include <random>

// Функции для поиска подстроки
bool find_substring_in_block(const std::string& block, const std::string& substring) {
  return block.find(substring) != std::string::npos;
}

// Структуры и функции для алгоритма Дейкстры
using Graph = std::vector<std::vector<std::pair<int, int>>>;

Graph generate_large_graph(int num_vertices, int num_edges) {
  Graph graph(num_vertices);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> vertex_dist(0, num_vertices - 1);
  std::uniform_int_distribution<> weight_dist(1, 100);

  for (int i = 0; i < num_edges; ++i) {
    int u = vertex_dist(gen);
    int v = vertex_dist(gen);
    int weight = weight_dist(gen);
    graph[u].push_back({v, weight});
  }

  return graph;
}

void dijkstra(const Graph& graph, int start, std::vector<int>& distances) {
  int n = graph.size();
  distances.assign(n, std::numeric_limits<int>::max());
  distances[start] = 0;

  std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>> pq;
  pq.push({0, start});

  while (!pq.empty()) {
    int current_distance = pq.top().first;
    int u = pq.top().second;
    pq.pop();

    if (current_distance > distances[u]) {
      continue;
    }

    for (const auto& [v, weight] : graph[u]) {
      if (distances[v] > distances[u] + weight) {
        distances[v] = distances[u] + weight;
        pq.push({distances[v], v});
      }
    }
  }
}

// Комбинированный бенчмарк
static void BM_CombinedBenchmark(benchmark::State& state) {
  // Параметры для поиска подстроки
  const std::string filename = "large_text_file.txt"; 
  const std::string substring = "target_substring";
  const size_t block_size = state.range(0);

  // Параметры для алгоритма Дейкстры
  int num_vertices = 10000;
  int num_edges = 100000;
  Graph graph = generate_large_graph(num_vertices, num_edges);
  int start_node = 0;
  std::vector<int> distances;

  for (auto _ : state) {
    // Поиск подстроки
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      state.SkipWithError("Failed to open file");
      return;
    }

    std::vector<char> buffer(block_size);
    bool found = false;

    while (file.read(buffer.data(), block_size)) {
      std::string block(buffer.data(), file.gcount());
      if (find_substring_in_block(block, substring)) {
        found = true;
        break;
      }
    }

    if (!found && file.gcount() > 0) {
      std::string block(buffer.data(), file.gcount());
      if (find_substring_in_block(block, substring)) {
        found = true;
      }
    }

    file.close();

    if (!found) {
      state.SkipWithError("Substring not found");
    }

    // Алгоритм Дейкстры
    dijkstra(graph, start_node, distances);
  }
}

// Регистрируем комбинированный бенчмарк с разными размерами блоков
BENCHMARK(BM_CombinedBenchmark)->Arg(1024)->Arg(4096)->Arg(16384)->Arg(65536)->Iterations(100);

BENCHMARK_MAIN();
