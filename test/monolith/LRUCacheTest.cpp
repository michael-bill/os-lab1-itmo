#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "lru_cache/file_operations.h"

constexpr const char* TEST_FILE = "test_file.bin";
constexpr size_t BLOCK_SIZE = 4096;

class LRUCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Создаем тестовый файл с известным содержимым
    int fd = open(TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    const char init_data[] = "Hello World!";
    write(fd, init_data, sizeof(init_data));
    close(fd);
  }

  void TearDown() override {
    unlink(TEST_FILE);
  }
};

// 1. Тест открытия и закрытия файла
TEST_F(LRUCacheTest, OpenCloseFile) {
  int fd = lab2_open(TEST_FILE);
  ASSERT_GT(fd, 0) << "Failed to open file";

  // Проверка доступности файла в кэше
  EXPECT_EQ(lab2_close(fd), 0) << "Failed to close file";

  // Попытка закрыть уже закрытый файл
  EXPECT_EQ(lab2_close(fd), -1) << "Should not close already closed file";
  EXPECT_EQ(errno, EBADF) << "Wrong error code";
}

// 2. Чтение записанных данных
TEST_F(LRUCacheTest, ReadWrittenData) {
  const char test_data[] = "Test read after write";
  char buffer[sizeof(test_data)] = {0};

  int fd = lab2_open(TEST_FILE);
  ASSERT_GT(fd, 0);

  // Запись через кэш
  ssize_t written = lab2_write(fd, test_data, sizeof(test_data));
  ASSERT_EQ(written, sizeof(test_data));

  // Сброс позиции и чтение
  lab2_lseek(fd, 0, SEEK_SET);
  ssize_t read = lab2_read(fd, buffer, sizeof(test_data));
  ASSERT_EQ(read, sizeof(test_data));
  EXPECT_STREQ(buffer, test_data) << "Data mismatch";

  lab2_close(fd);
}

// 3. Проверка записи на диск
TEST_F(LRUCacheTest, WritePersistanceCheck) {
  const char test_data[] = "Data should persist after close";
  char buffer[sizeof(test_data)] = {0};

  // Запись через кэш
  int fd = lab2_open(TEST_FILE);
  ASSERT_GT(fd, 0);
  lab2_write(fd, test_data, sizeof(test_data));
  lab2_close(fd);

  // Чтение через системный вызов
  int sys_fd = open(TEST_FILE, O_RDONLY);
  pread(sys_fd, buffer, sizeof(test_data), 0);
  close(sys_fd);

  EXPECT_STREQ(buffer, test_data) << "Data not persisted to disk";
}

// 4. Тест операций seek
TEST_F(LRUCacheTest, SeekOperations) {
  int fd = lab2_open(TEST_FILE);
  ASSERT_GT(fd, 0);

  // SEEK_SET
  off_t pos = lab2_lseek(fd, 5, SEEK_SET);
  EXPECT_EQ(pos, 5);

  // SEEK_CUR
  pos = lab2_lseek(fd, 3, SEEK_CUR);
  EXPECT_EQ(pos, 8);

  // SEEK_END
  struct stat st;
  fstat(open(TEST_FILE, O_RDONLY), &st);
  pos = lab2_lseek(fd, -4, SEEK_END);
  EXPECT_EQ(pos, st.st_size - 4);

  lab2_close(fd);
}

// 5. Проверка согласованности данных (один файл, два дескриптора)
TEST_F(LRUCacheTest, DataConsistency) {
  const char data1[] = "First write";
  const char data2[] = "Second write";
  char buffer[sizeof(data2)] = {0};

  int fd1 = lab2_open(TEST_FILE);
  int fd2 = lab2_open(TEST_FILE);

  // Запись через первый дескриптор
  lab2_write(fd1, data1, sizeof(data1));

  // Чтение через второй дескриптор (должно быть из кэша)
  lab2_read(fd2, buffer, sizeof(data1));
  EXPECT_STREQ(buffer, data1) << "Cache inconsistency between descriptors";

  // Синхронизация и проверка на диске
  lab2_fsync(fd1);
  int sys_fd = open(TEST_FILE, O_RDONLY);
  pread(sys_fd, buffer, sizeof(data1), 0);
  close(sys_fd);
  EXPECT_STREQ(buffer, data1) << "Fsync failed";

  lab2_close(fd1);
  lab2_close(fd2);
}

// 6. Тест переполнения кэша
TEST_F(LRUCacheTest, CacheEviction) {
  constexpr int NUM_BLOCKS = 10;
  char buffer[BLOCK_SIZE] = {0};

  int fd = lab2_open(TEST_FILE);

  // Запись большего количества блоков, чем вмещает кэш
  for (int i = 0; i < NUM_BLOCKS; ++i) {
    lab2_write(fd, buffer, BLOCK_SIZE);
  }

  // Проверка, что первые блоки были вытеснены
  lab2_lseek(fd, 0, SEEK_SET);
  ssize_t read = lab2_read(fd, buffer, BLOCK_SIZE);
  EXPECT_EQ(read, BLOCK_SIZE) << "First block should be reloaded";

  lab2_close(fd);
}

// 7. Тест работы с несколькими файлами
TEST_F(LRUCacheTest, MultipleFilesHandling) {
  const char* file1 = "file1.bin";
  const char* file2 = "file2.bin";

  int fd1 = lab2_open(file1);
  int fd2 = lab2_open(file2);

  const char data1[] = "Data for file1";
  const char data2[] = "Data for file2";

  lab2_write(fd1, data1, sizeof(data1));
  lab2_write(fd2, data2, sizeof(data2));

  char buffer[sizeof(data1)] = {0};

  lab2_lseek(fd1, 0, SEEK_SET);
  lab2_read(fd1, buffer, sizeof(data1));
  EXPECT_STREQ(buffer, data1);

  lab2_lseek(fd2, 0, SEEK_SET);
  lab2_read(fd2, buffer, sizeof(data2));
  EXPECT_STREQ(buffer, data2);

  lab2_close(fd1);
  lab2_close(fd2);

  unlink(file1);
  unlink(file2);
}

// 8. Тест повторного открытия файла
TEST_F(LRUCacheTest, ReopenFile) {
  const char data[] = "Reopen test data";

  int fd = lab2_open(TEST_FILE);
  lab2_write(fd, data, sizeof(data));
  lab2_close(fd);

  fd = lab2_open(TEST_FILE);
  char buffer[sizeof(data)] = {0};
  lab2_read(fd, buffer, sizeof(data));
  EXPECT_STREQ(buffer, data);
  lab2_close(fd);
}
