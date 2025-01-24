#include "lru_cache/lru_cache.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>

LRUCache::LRUCache(size_t block_size, size_t capacity)
    : block_size(block_size), capacity(capacity), next_fd(1) {
}

int LRUCache::openFile(const char* path) {
  int system_fd = ::open(path, O_RDWR | O_CREAT, 0644);
  if (system_fd == -1)
    return -1;

  struct stat st;
  if (fstat(system_fd, &st) == -1) {
    ::close(system_fd);
    return -1;
  }

  ino_t inode = st.st_ino;
  auto inode_it = inode_map.find(inode);
  if (inode_it != inode_map.end()) {
    ::close(system_fd);
    inode_it->second.refcount++;
  } else {
    inode_map[inode] = {system_fd, 1, st.st_size};
  }

  int fd = next_fd++;
  open_files[fd] = {0, inode};
  return fd;
}

int LRUCache::closeFile(int fd) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    errno = EBADF;
    return -1;
  }

  FileDescriptorInfo& file_info = file_it->second;
  ino_t inode = file_info.inode;

  syncFile(fd);

  auto inode_it = inode_map.find(inode);
  if (inode_it != inode_map.end()) {
    inode_it->second.refcount--;
    if (inode_it->second.refcount == 0) {
      for (auto& [key, block] : cache_map) {
        if (key.first == inode && block.dirty) {
          off_t offset = key.second * block_size;
          pwrite(inode_it->second.system_fd, block.data.data(), block_size, offset);
          block.dirty = false;
        }
      }
      fsync(inode_it->second.system_fd);
      ::close(inode_it->second.system_fd);
      inode_map.erase(inode_it);
    }
  }

  open_files.erase(file_it);
  return 0;
}

ssize_t LRUCache::readFile(int fd, void* buf, size_t count) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    errno = EBADF;
    return -1;
  }

  FileDescriptorInfo& file_info = file_it->second;
  ino_t inode = file_info.inode;
  auto inode_it = inode_map.find(inode);
  if (inode_it == inode_map.end()) {
    errno = EBADF;
    return -1;
  }

  off_t logical_size = inode_it->second.logical_size;
  if (file_info.current_pos >= logical_size)
    return 0;

  size_t bytes_remaining =
      std::min(count, static_cast<size_t>(logical_size - file_info.current_pos));
  size_t total_read = 0;
  char* buffer = static_cast<char*>(buf);

  while (bytes_remaining > 0) {
    size_t block_num = file_info.current_pos / block_size;
    size_t block_offset = file_info.current_pos % block_size;
    size_t bytes_to_read = std::min(block_size - block_offset, bytes_remaining);

    Key key{inode, block_num};
    auto cache_it = cache_map.find(key);
    if (cache_it == cache_map.end()) {
      if (!loadBlockFromDisk(fd, block_num)) {
        if (total_read == 0)
          return -1;
        break;
      }
      cache_it = cache_map.find(key);
    }

    CacheBlock& block = cache_it->second;
    memcpy(buffer + total_read, block.data.data() + block_offset, bytes_to_read);
    lru_list.splice(lru_list.begin(), lru_list, block.lru_iterator);

    total_read += bytes_to_read;
    file_info.current_pos += bytes_to_read;
    bytes_remaining -= bytes_to_read;
  }

  return total_read;
}

ssize_t LRUCache::writeFile(int fd, const void* buf, size_t count) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    errno = EBADF;
    return -1;
  }

  FileDescriptorInfo& file_info = file_it->second;
  ino_t inode = file_info.inode;
  auto inode_it = inode_map.find(inode);
  if (inode_it == inode_map.end()) {
    errno = EBADF;
    return -1;
  }

  const char* buffer = static_cast<const char*>(buf);
  size_t total_written = 0;

  while (count > 0) {
    size_t block_num = file_info.current_pos / block_size;
    size_t block_offset = file_info.current_pos % block_size;
    size_t bytes_to_write = std::min(block_size - block_offset, count);

    Key key{inode, block_num};
    auto cache_it = cache_map.find(key);
    if (cache_it == cache_map.end()) {
      if (!loadBlockForWrite(fd, block_num)) {
        if (total_written == 0)
          return -1;
        break;
      }
      cache_it = cache_map.find(key);
    }

    CacheBlock& block = cache_it->second;
    memcpy(block.data.data() + block_offset, buffer + total_written, bytes_to_write);
    block.dirty = true;
    lru_list.splice(lru_list.begin(), lru_list, block.lru_iterator);

    total_written += bytes_to_write;
    file_info.current_pos += bytes_to_write;
    if (file_info.current_pos > inode_it->second.logical_size) {
      inode_it->second.logical_size = file_info.current_pos;
    }
    count -= bytes_to_write;
  }

  return total_written;
}

off_t LRUCache::seekFile(int fd, off_t offset, int whence) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    errno = EBADF;
    return -1;
  }

  FileDescriptorInfo& file_info = file_it->second;
  ino_t inode = file_info.inode;
  auto inode_it = inode_map.find(inode);
  if (inode_it == inode_map.end()) {
    errno = EBADF;
    return -1;
  }

  off_t new_pos;
  off_t logical_size = inode_it->second.logical_size;

  switch (whence) {
    case SEEK_SET:
      new_pos = offset;
      break;
    case SEEK_CUR:
      new_pos = file_info.current_pos + offset;
      break;
    case SEEK_END:
      new_pos = logical_size + offset;
      break;
    default:
      errno = EINVAL;
      return -1;
  }

  if (new_pos < 0) {
    errno = EINVAL;
    return -1;
  }

  file_info.current_pos = new_pos;
  return new_pos;
}

int LRUCache::syncFile(int fd) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    errno = EBADF;
    return -1;
  }

  ino_t inode = file_it->second.inode;
  auto inode_it = inode_map.find(inode);
  if (inode_it == inode_map.end()) {
    errno = EBADF;
    return -1;
  }

  int system_fd = inode_it->second.system_fd;

  for (auto& [key, block] : cache_map) {
    if (key.first == inode && block.dirty) {
      off_t offset = key.second * block_size;
      if (pwrite(system_fd, block.data.data(), block_size, offset) == -1) {
        return -1;
      }
      block.dirty = false;
    }
  }

  if (fsync(system_fd) == -1) {
    return -1;
  }
  return 0;
}

void LRUCache::evict() {
  if (lru_list.empty())
    return;

  Key lru_key = lru_list.back();
  CacheBlock& block = cache_map[lru_key];
  if (block.dirty) {
    auto inode_it = inode_map.find(lru_key.first);
    if (inode_it != inode_map.end()) {
      off_t offset = lru_key.second * block_size;
      pwrite(inode_it->second.system_fd, block.data.data(), block_size, offset);
    }
  }

  auto& blocks = file_blocks[lru_key.first];
  blocks.erase(lru_key.second);
  if (blocks.empty()) {
    file_blocks.erase(lru_key.first);
  }

  cache_map.erase(lru_key);
  lru_list.pop_back();
}

bool LRUCache::loadBlockFromDisk(int fd, size_t block_num) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    return false;
  }

  ino_t inode = file_it->second.inode;
  auto inode_it = inode_map.find(inode);
  if (inode_it == inode_map.end()) {
    return false;
  }

  int system_fd = inode_it->second.system_fd;
  off_t logical_size = inode_it->second.logical_size;

  Key key{inode, block_num};
  std::vector<char> data(block_size, 0);
  off_t offset = block_num * block_size;

  if (offset < logical_size) {
    ssize_t bytes_read = pread(system_fd, data.data(), block_size, offset);
    if (bytes_read == -1)
      return false;
  }

  if (cache_map.size() >= capacity)
    evict();

  lru_list.push_front(key);
  cache_map[key] = {key, std::move(data), false, lru_list.begin()};
  file_blocks[inode].insert(block_num);
  return true;
}

bool LRUCache::loadBlockForWrite(int fd, size_t block_num) {
  auto file_it = open_files.find(fd);
  if (file_it == open_files.end()) {
    return false;
  }

  ino_t inode = file_it->second.inode;
  auto inode_it = inode_map.find(inode);
  if (inode_it == inode_map.end()) {
    return false;
  }

  int system_fd = inode_it->second.system_fd;
  off_t logical_size = inode_it->second.logical_size;

  Key key{inode, block_num};
  std::vector<char> data(block_size, 0);
  off_t offset = block_num * block_size;

  if (offset < logical_size) {
    ssize_t bytes_read = pread(system_fd, data.data(), block_size, offset);
    if (bytes_read == -1)
      return false;
  }

  if (cache_map.size() >= capacity)
    evict();

  lru_list.push_front(key);
  cache_map[key] = {key, std::move(data), true, lru_list.begin()};
  file_blocks[inode].insert(block_num);
  return true;
}

off_t LRUCache::getFileSize(int system_fd) const {
  struct stat st;
  return fstat(system_fd, &st) == 0 ? st.st_size : -1;
}
