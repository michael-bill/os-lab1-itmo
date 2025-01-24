#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class LRUCache {
public:
  LRUCache(size_t block_size, size_t capacity);
  int openFile(const char* path);
  int closeFile(int fd);
  ssize_t readFile(int fd, void* buf, size_t count);
  ssize_t writeFile(int fd, const void* buf, size_t count);
  off_t seekFile(int fd, off_t offset, int whence);
  int syncFile(int fd);

private:
  struct FileDescriptorInfo {
    off_t current_pos;
    ino_t inode;
  };

  struct InodeInfo {
    int system_fd;
    int refcount;
    off_t logical_size;
  };

  using Key = std::pair<ino_t, size_t>;

  struct CacheBlock {
    Key key;
    std::vector<char> data;
    bool dirty;
    std::list<Key>::iterator lru_iterator;
  };

  struct KeyHash {
    size_t operator()(const Key& k) const {
      return std::hash<ino_t>()(k.first) ^ std::hash<size_t>()(k.second);
    }
  };

  size_t block_size;
  size_t capacity;
  std::unordered_map<int, FileDescriptorInfo> open_files;
  std::unordered_map<ino_t, InodeInfo> inode_map;
  std::unordered_map<Key, CacheBlock, KeyHash> cache_map;
  std::list<Key> lru_list;
  std::unordered_map<ino_t, std::unordered_set<size_t>> file_blocks;
  int next_fd;

  void evict();
  bool loadBlockFromDisk(int fd, size_t block_num);
  bool loadBlockForWrite(int fd, size_t block_num);
  off_t getFileSize(int system_fd) const;
};

#endif  // LRU_CACHE_H
