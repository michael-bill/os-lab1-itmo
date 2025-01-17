get_filename_component(MONOLITH_BENCH_SOURCE_PATH "./${PROJECT_NAME}" ABSOLUTE)

set(MONOLITH_BENCH_TARGET_BASE "${PROJECT_NAME}-bench")

# Отключаем любые оптимизации
add_compile_options(-O0)

macro(MONOLITH_BENCH_DECLARE suffix file)
    add_executable(
        ${MONOLITH_BENCH_TARGET_BASE}-${suffix}
        ${MONOLITH_BENCH_SOURCE_PATH}/${file}
    )

    target_link_libraries(
        ${MONOLITH_BENCH_TARGET_BASE}-${suffix}
        ${PROJECT_NAME}
        benchmark::benchmark
    )
endmacro()

MONOLITH_BENCH_DECLARE(first-test FirstTestBench.cpp)
MONOLITH_BENCH_DECLARE(ema-search-str EmaSearchStrBench.cpp)
MONOLITH_BENCH_DECLARE(short-path ShortPathBench.cpp)
