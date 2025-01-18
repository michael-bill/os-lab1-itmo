#include <benchmark/benchmark.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Функция для поиска подстроки в блоке данных
bool find_substring_in_block(const std::string& block, const std::string& substring) {
  return block.find(substring) != std::string::npos;
}

// Бенчмарк для поиска подстроки в файле с чтением блоками
static void BM_SubstringSearchInFile(benchmark::State& state) {
  const std::string filename = "large_text_file.txt"; 
  const std::string substring = "target_substring";
  const size_t block_size = state.range(0);

  for (auto _ : state) {
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

    // Проверяем последний блок, если он неполный
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
  }
}

// Регистрируем бенчмарк с разными размерами блоков
BENCHMARK(BM_SubstringSearchInFile)->Arg(1024)->Arg(4096)->Arg(16384)->Arg(65536)->Iterations(1);;

BENCHMARK_MAIN();
