cmake_minimum_required(VERSION 3.16)

project(galay-kernel VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建选项 / 平台选项
include(cmake/option.cmake)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 复制 scripts 目录到构建目录
file(COPY ${CMAKE_SOURCE_DIR}/scripts
     DESTINATION ${CMAKE_BINARY_DIR}
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# 查找spdlog
if(ENABLE_LOG)
    find_package(spdlog QUIET)
    if(NOT spdlog_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.12.0
        )
        FetchContent_MakeAvailable(spdlog)
    endif()

    # Prefer header-only to avoid linking against a non-PIC libspdlog.a when building shared libs.
    if(SPDLOG_USE_HEADER_ONLY)
        if(TARGET spdlog::spdlog_header_only)
            set(GALAY_SPDLOG_TARGET spdlog::spdlog_header_only)
        elseif(TARGET spdlog::spdlog)
            # Some spdlog package installs don't export spdlog::spdlog_header_only. Create a shim
            # that reuses spdlog::spdlog's usage requirements but forces header-only mode.
            add_library(galay_spdlog_header_only INTERFACE)

            get_target_property(_galay_spdlog_incs spdlog::spdlog INTERFACE_INCLUDE_DIRECTORIES)
            if(_galay_spdlog_incs)
                target_include_directories(galay_spdlog_header_only INTERFACE ${_galay_spdlog_incs})
            endif()

            get_target_property(_galay_spdlog_links spdlog::spdlog INTERFACE_LINK_LIBRARIES)
            if(_galay_spdlog_links)
                # Don't accidentally re-introduce the compiled spdlog library via usage requirements.
                list(REMOVE_ITEM _galay_spdlog_links spdlog::spdlog)
                target_link_libraries(galay_spdlog_header_only INTERFACE ${_galay_spdlog_links})
            endif()

            get_target_property(_galay_spdlog_defs spdlog::spdlog INTERFACE_COMPILE_DEFINITIONS)
            if(_galay_spdlog_defs)
                list(REMOVE_ITEM _galay_spdlog_defs SPDLOG_COMPILED_LIB SPDLOG_SHARED_LIB SPDLOG_EXPORTS)
                target_compile_definitions(galay_spdlog_header_only INTERFACE ${_galay_spdlog_defs})
            endif()
            target_compile_definitions(galay_spdlog_header_only INTERFACE SPDLOG_HEADER_ONLY)

            add_library(spdlog::spdlog_header_only ALIAS galay_spdlog_header_only)
            set(GALAY_SPDLOG_TARGET spdlog::spdlog_header_only)
        else()
            message(FATAL_ERROR "ENABLE_LOG=ON but spdlog targets were not created by find_package()/FetchContent.")
        endif()
    else()
        if(TARGET spdlog::spdlog)
            set(GALAY_SPDLOG_TARGET spdlog::spdlog)
        elseif(TARGET spdlog::spdlog_header_only)
            set(GALAY_SPDLOG_TARGET spdlog::spdlog_header_only)
        else()
            message(FATAL_ERROR "ENABLE_LOG=ON but spdlog targets were not created by find_package()/FetchContent.")
        endif()
    endif()

    add_compile_definitions(USE_LOG)
endif()

# 配置IO后端
configure_io_backend()

# 包含子目录
add_subdirectory(galay-kernel)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
