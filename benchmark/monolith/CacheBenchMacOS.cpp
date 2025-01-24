#include <benchmark/benchmark.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <random>
#include <vector>

#include "lru_cache/file_operations.h"

constexpr size_t FILE_SIZE = 64ULL * 1024ULL * 1024ULL;  // 64 mb
constexpr size_t BLOCK_SIZE = 4096;
constexpr int RAND_SEED = 1703;
const int repeat = FILE_SIZE / BLOCK_SIZE;  // повторения на чтение или запись = количество блоков

// Отключение системного кэширования
void disable_system_cache(int fd) {
  fcntl(fd, F_NOCACHE, 1);
}

// Создание тестового файла
void create_test_file(const char* filename) {
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  for (size_t i = 0; i < FILE_SIZE; i += BLOCK_SIZE) {
    write(fd, std::vector<char>(BLOCK_SIZE).data(), BLOCK_SIZE);
  }
  close(fd);
}

// Базовые фикстуры
class BaseFixture : public benchmark::Fixture {
protected:
  void SetUp(const benchmark::State&) override {
    system("mkdir -p test_data");
    create_test_file("test_data/large_file.bin");
  }

  void TearDown(const benchmark::State&) override {
    system("rm -rf test_data");
  }
};

// Случайное чтение
BENCHMARK_DEFINE_F(BaseFixture, RandomRead_Cached)(benchmark::State& state) {
  int fd = lab2_open("test_data/large_file.bin");
  disable_system_cache(fd);
  std::mt19937 gen(RAND_SEED);
  std::uniform_int_distribution<size_t> dist(0, (FILE_SIZE - BLOCK_SIZE) * 10);

  // Выделяем буфер один раз
  char* buf = new char[BLOCK_SIZE];

  for (auto _ : state) {
    for (int i = 0; i < repeat; ++i) {
      off_t offset = static_cast<off_t>(dist(gen) % (FILE_SIZE - BLOCK_SIZE));
      lab2_lseek(fd, offset, SEEK_SET);
      lab2_read(fd, buf, BLOCK_SIZE);
    }
  }

  delete[] buf;
  lab2_close(fd);
}

BENCHMARK_DEFINE_F(BaseFixture, RandomRead_Uncached)(benchmark::State& state) {
  int fd = open("test_data/large_file.bin", O_RDONLY);
  disable_system_cache(fd);
  char* buf = new char[BLOCK_SIZE];
  std::mt19937 gen(RAND_SEED);
  std::uniform_int_distribution<size_t> dist(0, (FILE_SIZE - BLOCK_SIZE) * 10);

  for (auto _ : state) {
    for (int i = 0; i < repeat; ++i) {
      off_t offset = static_cast<off_t>(dist(gen) % (FILE_SIZE - BLOCK_SIZE));
      pread(fd, buf, BLOCK_SIZE, offset);
    }
  }

  delete[] buf;
  close(fd);
}

// Смешанная нагрузка
BENCHMARK_DEFINE_F(BaseFixture, MixedWorkload_Cached)(benchmark::State& state) {
  int fd = lab2_open("test_data/large_file.bin");
  disable_system_cache(fd);
  std::mt19937 gen(RAND_SEED);
  std::uniform_int_distribution<size_t> pos_dist(0, FILE_SIZE * 10);
  std::bernoulli_distribution op_dist(0.7);  // 70% чтения, 30% записи

  // Выделяем буферы один раз
  char* read_buf = new char[BLOCK_SIZE];
  const char* write_buf = new char[BLOCK_SIZE]{};

  for (auto _ : state) {
    for (int i = 0; i < repeat; ++i) {
      off_t offset = static_cast<off_t>(pos_dist(gen) % (FILE_SIZE - BLOCK_SIZE));

      if (op_dist(gen)) {  // Чтение
        lab2_lseek(fd, offset, SEEK_SET);
        lab2_read(fd, read_buf, BLOCK_SIZE);
      } else {  // Запись
        lab2_lseek(fd, offset, SEEK_SET);
        lab2_write(fd, write_buf, BLOCK_SIZE);
      }
    }
  }

  delete[] read_buf;
  delete[] write_buf;
  lab2_close(fd);
}

BENCHMARK_DEFINE_F(BaseFixture, MixedWorkload_Uncached)(benchmark::State& state) {
  int fd = open("test_data/large_file.bin", O_RDWR);
  disable_system_cache(fd);
  std::mt19937 gen(RAND_SEED);
  std::uniform_int_distribution<size_t> pos_dist(0, FILE_SIZE * 10);
  std::bernoulli_distribution op_dist(0.7);  // 70% чтения, 30% записи

  char* read_buf = new char[BLOCK_SIZE];
  const char* write_buf = new char[BLOCK_SIZE]{};

  for (auto _ : state) {
    for (int i = 0; i < repeat; ++i) {
      off_t offset = static_cast<off_t>(pos_dist(gen) % (FILE_SIZE - BLOCK_SIZE));
      if (op_dist(gen)) {
        pread(fd, read_buf, BLOCK_SIZE, offset);
      } else {
        pwrite(fd, write_buf, BLOCK_SIZE, offset);
        fsync(fd);
      }
    }
  }

  delete[] read_buf;
  delete[] write_buf;
  close(fd);
}

// Повторяющийся доступ к небольшому набору данных (благоприятный для LRU)
BENCHMARK_DEFINE_F(BaseFixture, TightAreaRandomRead_Cached)(benchmark::State& state) {
  int fd = lab2_open("test_data/large_file.bin");
  disable_system_cache(fd);
  const size_t hot_region_size = 10 * BLOCK_SIZE;  // 10 блоков для повторяющегося доступа
  std::mt19937 gen(RAND_SEED);
  std::uniform_int_distribution<size_t> dist(0, hot_region_size - BLOCK_SIZE);

  // Выделяем буфер один раз
  char* buf = new char[BLOCK_SIZE];

  for (auto _ : state) {
    for (int i = 0; i < repeat; ++i) {
      off_t offset = static_cast<off_t>(dist(gen));  // Доступ только к "горячей" области
      lab2_lseek(fd, offset, SEEK_SET);
      lab2_read(fd, buf, BLOCK_SIZE);
    }
  }

  delete[] buf;
  lab2_close(fd);
}

BENCHMARK_DEFINE_F(BaseFixture, TightAreaRandomRead_Uncached)(benchmark::State& state) {
  int fd = open("test_data/large_file.bin", O_RDONLY);
  disable_system_cache(fd);
  const size_t hot_region_size = 10 * BLOCK_SIZE;  // 10 блоков для повторяющегося доступа
  std::mt19937 gen(RAND_SEED);
  std::uniform_int_distribution<size_t> dist(0, hot_region_size - BLOCK_SIZE);

  char* buf = new char[BLOCK_SIZE];

  for (auto _ : state) {
    for (int i = 0; i < repeat; ++i) {
      off_t offset = static_cast<off_t>(dist(gen));  // Доступ только к "горячей" области
      pread(fd, buf, BLOCK_SIZE, offset);
    }
  }

  delete[] buf;
  close(fd);
}

// Прогрев кэша
BENCHMARK_DEFINE_F(BaseFixture, WarmedCache)(benchmark::State& state) {
  int fd = lab2_open("test_data/large_file.bin");
  disable_system_cache(fd);
  // Прогрев
  char* temp = new char[BLOCK_SIZE];
  for (size_t pos = 0; pos < FILE_SIZE; pos += BLOCK_SIZE) {
    lab2_lseek(fd, static_cast<off_t>(pos), SEEK_SET);
    lab2_read(fd, temp, BLOCK_SIZE);
  }
  delete[] temp;

  // Тестирование
  for (auto _ : state) {
    char* buf = new char[BLOCK_SIZE];
    lab2_read(fd, buf, BLOCK_SIZE);
    delete[] buf;
  }

  lab2_close(fd);
}

BENCHMARK_DEFINE_F(BaseFixture, ColdCache)(benchmark::State& state) {
  int fd = open("test_data/large_file.bin", O_RDONLY);
  disable_system_cache(fd);
  void* buf;
  posix_memalign(&buf, BLOCK_SIZE, BLOCK_SIZE);

  for (auto _ : state) {
    pread(fd, buf, BLOCK_SIZE, 0);
    fsync(fd);
  }

  free(buf);
  close(fd);
}

// Последовательное чтение
BENCHMARK_DEFINE_F(BaseFixture, SequentialRead_Cached)(benchmark::State& state) {
  int fd = lab2_open("test_data/large_file.bin");
  disable_system_cache(fd);

  for (auto _ : state) {
    char* buf = new char[BLOCK_SIZE];
    size_t pos = 0;
    while (pos < FILE_SIZE) {
      lab2_lseek(fd, static_cast<off_t>(pos), SEEK_SET);
      lab2_read(fd, buf, BLOCK_SIZE);
      pos += BLOCK_SIZE;
    }
    delete[] buf;
  }

  lab2_close(fd);
}

BENCHMARK_DEFINE_F(BaseFixture, SequentialRead_Uncached)(benchmark::State& state) {
  int fd = open("test_data/large_file.bin", O_RDONLY);
  disable_system_cache(fd);

  void* buf;
  posix_memalign(&buf, BLOCK_SIZE, BLOCK_SIZE);

  for (auto _ : state) {
    size_t pos = 0;
    while (pos < FILE_SIZE) {
      pread(fd, buf, BLOCK_SIZE, static_cast<off_t>(pos));
      pos += BLOCK_SIZE;
    }
  }

  free(buf);
  close(fd);
}

// Регистрация тестов
BENCHMARK_REGISTER_F(BaseFixture, RandomRead_Cached)->Unit(benchmark::kMillisecond)->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, RandomRead_Uncached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, MixedWorkload_Cached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, MixedWorkload_Uncached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, TightAreaRandomRead_Cached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, TightAreaRandomRead_Uncached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, WarmedCache)->Unit(benchmark::kMicrosecond)->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, ColdCache)->Unit(benchmark::kMicrosecond)->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, SequentialRead_Cached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);
BENCHMARK_REGISTER_F(BaseFixture, SequentialRead_Uncached)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);

BENCHMARK_MAIN();
