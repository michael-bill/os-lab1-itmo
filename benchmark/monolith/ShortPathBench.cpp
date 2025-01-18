#include <benchmark/benchmark.h>

#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <vector>

// Структура для хранения графа (список смежности)
using Graph = std::vector<std::vector<std::pair<int, int>>>;  // {вершина, вес}

// Функция для генерации случайного графа
Graph generate_large_graph(int num_vertices, int num_edges) {
  Graph graph(num_vertices);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> vertex_dist(0, num_vertices - 1);
  std::uniform_int_distribution<> weight_dist(1, 100);  // Веса рёбер от 1 до 100

  for (int i = 0; i < num_edges; ++i) {
    int u = vertex_dist(gen);
    int v = vertex_dist(gen);
    int weight = weight_dist(gen);
    graph[u].push_back({v, weight});
  }

  return graph;
}

// Алгоритм Дейкстры для поиска кратчайшего пути
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

// Бенчмарк для поиска кратчайшего пути
static void BM_Dijkstra(benchmark::State& state) {
  int num_vertices = 10000;  // Количество вершин
  int num_edges = 100000;    // Количество рёбер

  // Генерация большого графа
  Graph graph = generate_large_graph(num_vertices, num_edges);

  int start_node = 0;  // Начальная вершина
  std::vector<int> distances;

  for (auto _ : state) {
    dijkstra(graph, start_node, distances);
  }
}

// Регистрируем бенчмарк
BENCHMARK(BM_Dijkstra)->Iterations(100);;

BENCHMARK_MAIN();
