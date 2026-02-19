# galay-kernel 构建选项配置

# 设置默认安装目录为/usr/local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr/local CACHE PATH "Install path prefix" FORCE)
endif()

# 构建目标选项
option(BUILD_TESTS "Build test executables" ON)
option(BUILD_BENCHMARKS "Build benchmark executables" ON)
option(BUILD_EXAMPLES "Build example executables" ON)
option(ENABLE_CPP23_MODULES "Enable experimental C++23 named modules support" OFF)

# 库类型选项（CMake 标准变量）
option(BUILD_SHARED_LIBS "Build shared library instead of static" OFF)

# 日志选项
option(ENABLE_LOG "Enable logging with spdlog" ON)
# Keep this project header-only for spdlog to avoid linking against system libspdlog.a (often non-PIC).
set(SPDLOG_USE_HEADER_ONLY ON CACHE BOOL "Use spdlog header-only target (avoid linking libspdlog)" FORCE)

# IO后端选项
option(DISABLE_IOURING "Disable io_uring and use epoll on Linux" ON)

# 平台特定的异步IO后端检测
function(configure_io_backend)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(DISABLE_IOURING)
            message(STATUS "io_uring disabled by user, using epoll")
            add_compile_definitions(USE_EPOLL)
            set(GALAY_KERNEL_BACKEND "epoll" PARENT_SCOPE)
            # epoll 模式需要 libaio 支持异步文件IO
            find_library(AIO_LIB aio)
            if(AIO_LIB)
                message(STATUS "Found libaio: ${AIO_LIB}")
                set(PLATFORM_LIBS ${AIO_LIB} PARENT_SCOPE)
            else()
                message(WARNING "libaio not found, async file IO will not work")
            endif()
        else()
            find_library(URING_LIB uring)
            if(URING_LIB)
                message(STATUS "Found liburing: ${URING_LIB}, using io_uring")
                add_compile_definitions(USE_IOURING)
                set(GALAY_KERNEL_BACKEND "io_uring" PARENT_SCOPE)
                set(PLATFORM_LIBS ${URING_LIB} PARENT_SCOPE)
            else()
                message(STATUS "liburing not found, falling back to epoll")
                add_compile_definitions(USE_EPOLL)
                set(GALAY_KERNEL_BACKEND "epoll" PARENT_SCOPE)
                # fallback 到 epoll 时也需要 libaio
                find_library(AIO_LIB aio)
                if(AIO_LIB)
                    set(PLATFORM_LIBS ${AIO_LIB} PARENT_SCOPE)
                endif()
            endif()
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(STATUS "macOS detected, using kqueue")
        add_compile_definitions(USE_KQUEUE)
        set(GALAY_KERNEL_BACKEND "kqueue" PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(STATUS "Windows detected, using IOCP")
        add_compile_definitions(USE_IOCP)
        set(GALAY_KERNEL_BACKEND "iocp" PARENT_SCOPE)
    else()
        message(WARNING "Unknown platform: ${CMAKE_SYSTEM_NAME}")
        set(GALAY_KERNEL_BACKEND "unknown" PARENT_SCOPE)
    endif()
endfunction()
