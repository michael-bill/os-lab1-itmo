#include "lru_cache/file_operations.h"

#include "lru_cache/lru_cache.h"

static LRUCache cache(4096, 2048);

int lab2_open(const char* path) {
  return cache.openFile(path);
}

int lab2_close(int fd) {
  return cache.closeFile(fd);
}

ssize_t lab2_read(int fd, void* buf, size_t count) {
  return cache.readFile(fd, buf, count);
}

ssize_t lab2_write(int fd, const void* buf, size_t count) {
  return cache.writeFile(fd, buf, count);
}

off_t lab2_lseek(int fd, off_t offset, int whence) {
  return cache.seekFile(fd, offset, whence);
}

int lab2_fsync(int fd) {
  return cache.syncFile(fd);
}
