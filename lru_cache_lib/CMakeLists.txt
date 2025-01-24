project(lru_cache_lib)

# Добавляем библиотеку
add_library(lru_cache_lib STATIC
    src/lru_cache.cpp
    src/file_operations.cpp
)

# Указываем путь к заголовочным файлам
target_include_directories(lru_cache_lib PUBLIC ${CMAKE_SOURCE_DIR}/lru_cache_lib/include)

# Устанавливаем библиотеку в систему (опционально)
install(TARGETS lru_cache_lib DESTINATION lib)
install(DIRECTORY include/lru_cache DESTINATION include)
