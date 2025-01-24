#include "lru_cache/lru_cache.h"
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <algorithm>

LRUCache::LRUCache(size_t block_size, size_t capacity)
    : block_size(block_size), capacity(capacity), next_fd(1) {}

int LRUCache::openFile(const char *path) {
    int system_fd = ::open(path, O_RDWR | O_CREAT, 0644);
    if (system_fd == -1) return -1;

    struct stat st;
    if (fstat(system_fd, &st) == -1) {
        ::close(system_fd);
        return -1;
    }

    int fd = next_fd++;
    open_files[fd] = {
        system_fd,
        0,
        static_cast<off_t>(st.st_size),
        path
    };
    return fd;
}

int LRUCache::closeFile(int fd) {
    auto file_it = open_files.find(fd);
    if (file_it == open_files.end()) {
        errno = EBADF;
        return -1;
    }

    FileDescriptorInfo& file_info = file_it->second;
    syncFile(fd);

    auto& blocks = file_blocks[fd];
    for (auto block_num : blocks) {
        Key key{fd, block_num};
        auto cache_it = cache_map.find(key);
        if (cache_it != cache_map.end()) {
            lru_list.erase(cache_it->second.lru_iterator);
            cache_map.erase(cache_it);
        }
    }
    file_blocks.erase(fd);

    ::close(file_info.system_fd);
    open_files.erase(file_it);
    return 0;
}

ssize_t LRUCache::readFile(int fd, void *buf, size_t count) {
    auto file_it = open_files.find(fd);
    if (file_it == open_files.end()) {
        errno = EBADF;
        return -1;
    }

    FileDescriptorInfo& file_info = file_it->second;
    if (file_info.current_pos >= file_info.logical_size) return 0;

    size_t bytes_remaining = std::min(count, static_cast<size_t>(file_info.logical_size - file_info.current_pos));
    size_t total_read = 0;
    char* buffer = static_cast<char*>(buf);

    while (bytes_remaining > 0) {
        size_t block_num = file_info.current_pos / block_size;
        size_t block_offset = file_info.current_pos % block_size;
        size_t bytes_to_read = std::min(block_size - block_offset, bytes_remaining);

        Key key{fd, block_num};
        auto cache_it = cache_map.find(key);
        if (cache_it == cache_map.end()) {
            if (!loadBlockFromDisk(fd, block_num, file_info.system_fd, file_info.logical_size)) {
                if (total_read == 0) return -1;
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

ssize_t LRUCache::writeFile(int fd, const void *buf, size_t count) {
    auto file_it = open_files.find(fd);
    if (file_it == open_files.end()) {
        errno = EBADF;
        return -1;
    }

    FileDescriptorInfo& file_info = file_it->second;
    const char* buffer = static_cast<const char*>(buf);
    size_t total_written = 0;

    while (count > 0) {
        size_t block_num = file_info.current_pos / block_size;
        size_t block_offset = file_info.current_pos % block_size;
        size_t bytes_to_write = std::min(block_size - block_offset, count);

        Key key{fd, block_num};
        auto cache_it = cache_map.find(key);
        if (cache_it == cache_map.end()) {
            if (!loadBlockForWrite(fd, block_num, file_info.system_fd, file_info.logical_size)) {
                if (total_written == 0) return -1;
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
        if (file_info.current_pos > file_info.logical_size) {
            file_info.logical_size = file_info.current_pos;
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
    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = file_info.current_pos + offset;
            break;
        case SEEK_END:
            new_pos = file_info.logical_size + offset;
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

    FileDescriptorInfo& file_info = file_it->second;
    for (auto& [key, block] : cache_map) {
        if (key.first == fd && block.dirty) {
            off_t offset = key.second * block_size;
            if (pwrite(file_info.system_fd, block.data.data(), block_size, offset) == -1) {
                return -1;
            }
            block.dirty = false;
        }
    }

    if (fsync(file_info.system_fd) == -1) {
        return -1;
    }
    return 0;
}

void LRUCache::evict() {
    if (lru_list.empty()) return;

    Key lru_key = lru_list.back();
    CacheBlock& block = cache_map[lru_key];
    if (block.dirty) {
        auto file_it = open_files.find(lru_key.first);
        if (file_it != open_files.end()) {
            off_t offset = lru_key.second * block_size;
            pwrite(file_it->second.system_fd, block.data.data(), block_size, offset);
        }
    }

    file_blocks[lru_key.first].erase(lru_key.second);
    if (file_blocks[lru_key.first].empty()) {
        file_blocks.erase(lru_key.first);
    }

    cache_map.erase(lru_key);
    lru_list.pop_back();
}

bool LRUCache::loadBlockFromDisk(int fd, size_t block_num, int system_fd, off_t logical_size) {
    Key key{fd, block_num};
    std::vector<char> data(block_size, 0);
    off_t offset = block_num * block_size;

    if (offset < logical_size) {
        ssize_t bytes_read = pread(system_fd, data.data(), block_size, offset);
        if (bytes_read == -1) return false;
    }

    if (cache_map.size() >= capacity) evict();

    lru_list.push_front(key);
    cache_map[key] = {key, std::move(data), false, lru_list.begin()};
    file_blocks[fd].insert(block_num);
    return true;
}

bool LRUCache::loadBlockForWrite(int fd, size_t block_num, int system_fd, off_t logical_size) {
    Key key{fd, block_num};
    std::vector<char> data(block_size, 0);
    off_t offset = block_num * block_size;

    if (offset < logical_size) {
        ssize_t bytes_read = pread(system_fd, data.data(), block_size, offset);
        if (bytes_read == -1) return false;
    }

    if (cache_map.size() >= capacity) evict();

    lru_list.push_front(key);
    cache_map[key] = {key, std::move(data), true, lru_list.begin()};
    file_blocks[fd].insert(block_num);
    return true;
}

off_t LRUCache::getFileSize(int system_fd) const {
    struct stat st;
    return fstat(system_fd, &st) == 0 ? st.st_size : -1;
}
