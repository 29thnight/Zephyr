cmake_minimum_required(VERSION 3.20)
project(Zephyr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build options ────────────────────────────────────────────────────────────
option(ZEPHYR_BUILD_CLI     "Build zephyr_cli executable"    ON)
option(ZEPHYR_BUILD_TESTS   "Build zephyr_tests executable"  ON)
option(ZEPHYR_BUILD_BENCH   "Build zephyr_bench executable"  ON)
option(ZEPHYR_BUILD_SAMPLES "Build sample executables"       ON)
option(ZEPHYR_ENABLE_PGO    "Enable PGO instrumentation (MSVC /GL+/LTCG)" OFF)

# ─── ZephyrRuntime (static library) ──────────────────────────────────────────
set(ZEPHYR_RUNTIME_SOURCES
    src/zephyr.cpp
    src/zephyr_parser.cpp
    src/zephyr_gc_impl.cpp
)
# zephyr_vm_dispatch.c uses computed goto (GCC/Clang extension), not supported by MSVC
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    list(APPEND ZEPHYR_RUNTIME_SOURCES src/zephyr_vm_dispatch.c)
endif()
add_library(ZephyrRuntime STATIC ${ZEPHYR_RUNTIME_SOURCES})

target_include_directories(ZephyrRuntime
    PUBLIC  include
    PRIVATE src
)

if(MSVC)
    target_compile_options(ZephyrRuntime PRIVATE
        /W3 /permissive- /bigobj /utf-8
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /RTC1>
    )
    target_link_options(ZephyrRuntime INTERFACE
        $<$<CONFIG:Release>:/LTCG>
    )
    if(ZEPHYR_ENABLE_PGO)
        target_compile_options(ZephyrRuntime PRIVATE /GL)
        target_link_options(ZephyrRuntime INTERFACE /LTCG:PGI)
    endif()
else()
    target_compile_options(ZephyrRuntime PRIVATE
        -Wall -Wextra
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-O0 -g>
    )
endif()

# ─── zephyr_cli ──────────────────────────────────────────────────────────────
if(ZEPHYR_BUILD_CLI)
    add_executable(zephyr_cli
        cli/main.cpp
        cli/lsp_server.cpp
        cli/dap_server.cpp
        bench/bench_runner.cpp
    )
    target_link_libraries(zephyr_cli PRIVATE ZephyrRuntime)
    target_include_directories(zephyr_cli PRIVATE include bench)
    if(MSVC)
        target_compile_options(zephyr_cli PRIVATE /W3 /permissive- /bigobj
            $<$<CONFIG:Release>:/O2 /GL>)
        target_link_options(zephyr_cli PRIVATE $<$<CONFIG:Release>:/LTCG>)
    endif()
    set_target_properties(zephyr_cli PROPERTIES OUTPUT_NAME "zephyr")
endif()

# ─── zephyr_tests ─────────────────────────────────────────────────────────────
if(ZEPHYR_BUILD_TESTS)
    add_executable(zephyr_tests
        tests/test_main.cpp
        tests/test_lexer.cpp
        tests/test_compiler.cpp
        tests/test_vm.cpp
        tests/test_gc.cpp
        tests/test_host.cpp
        tests/test_perf.cpp
        tests/test_corpus.cpp
    )
    target_link_libraries(zephyr_tests PRIVATE ZephyrRuntime)
    target_include_directories(zephyr_tests PRIVATE include tests bench)
    if(MSVC)
        target_compile_options(zephyr_tests PRIVATE /W3 /permissive- /bigobj
            $<$<CONFIG:Release>:/O2>)
    endif()
    # Copy corpus to build dir so tests can find it
    add_custom_command(TARGET zephyr_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/tests/corpus"
            "$<TARGET_FILE_DIR:zephyr_tests>/corpus"
        COMMENT "Copying corpus to test output directory"
    )
endif()

# ─── zephyr_bench ────────────────────────────────────────────────────────────
if(ZEPHYR_BUILD_BENCH)
    add_executable(zephyr_bench
        bench/main.cpp
        bench/bench_runner.cpp
    )
    target_link_libraries(zephyr_bench PRIVATE ZephyrRuntime)
    target_include_directories(zephyr_bench PRIVATE include bench)
    if(MSVC)
        target_compile_options(zephyr_bench PRIVATE /W3 /permissive- /bigobj
            $<$<CONFIG:Release>:/O2 /GL>)
        target_link_options(zephyr_bench PRIVATE $<$<CONFIG:Release>:/LTCG>)
    else()
        target_compile_options(zephyr_bench PRIVATE
            -Wall -Wextra
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug>:-O0 -g>)
    endif()
    # Copy bench scripts to build dir
    add_custom_command(TARGET zephyr_bench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/bench/scripts"
            "$<TARGET_FILE_DIR:zephyr_bench>/bench/scripts"
        COMMENT "Copying bench scripts to output directory"
    )
endif()

# ─── zephyr_engine_sample ─────────────────────────────────────────────────────
if(ZEPHYR_BUILD_SAMPLES)
    add_executable(zephyr_engine_sample
        samples/engine_sample/main.cpp
    )
    target_link_libraries(zephyr_engine_sample PRIVATE ZephyrRuntime)
    target_include_directories(zephyr_engine_sample PRIVATE include)
    if(MSVC)
        target_compile_options(zephyr_engine_sample PRIVATE /W3 /permissive- /bigobj
            $<$<CONFIG:Release>:/O2 /GL>)
        target_link_options(zephyr_engine_sample PRIVATE $<$<CONFIG:Release>:/LTCG>)
    else()
        target_compile_options(zephyr_engine_sample PRIVATE
            -Wall -Wextra
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug>:-O0 -g>)
    endif()
    set_target_properties(zephyr_engine_sample PROPERTIES OUTPUT_NAME "zephyr_engine_sample")
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ZephyrRuntime
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/zephyr DESTINATION include)
if(ZEPHYR_BUILD_CLI)
    install(TARGETS zephyr_cli RUNTIME DESTINATION bin)
endif()
