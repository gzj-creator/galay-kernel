# 收集源文件
file(GLOB_RECURSE SOURCES
    "*/*.cc"
)

# 收集头文件
file(GLOB_RECURSE HEADERS
    "*/*.h"
    "*/*.hpp"
)

# 创建库目标
add_library(galay-kernel ${SOURCES} ${HEADERS})

# 导出统一后端宏给所有下游，避免头文件按平台自动推导造成 ABI 不一致。
if(GALAY_KERNEL_BACKEND STREQUAL "epoll")
    target_compile_definitions(galay-kernel PUBLIC USE_EPOLL)
elseif(GALAY_KERNEL_BACKEND STREQUAL "io_uring")
    target_compile_definitions(galay-kernel PUBLIC USE_IOURING)
elseif(GALAY_KERNEL_BACKEND STREQUAL "kqueue")
    target_compile_definitions(galay-kernel PUBLIC USE_KQUEUE)
elseif(GALAY_KERNEL_BACKEND STREQUAL "iocp")
    target_compile_definitions(galay-kernel PUBLIC USE_IOCP)
else()
    message(FATAL_ERROR "GALAY_KERNEL_BACKEND is not set correctly: ${GALAY_KERNEL_BACKEND}")
endif()

# 设置包含目录
target_include_directories(galay-kernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/galay-kernel>
)

# 链接spdlog
if(ENABLE_LOG)
    if(GALAY_SPDLOG_TARGET)
        target_link_libraries(galay-kernel PUBLIC ${GALAY_SPDLOG_TARGET})
    else()
        message(FATAL_ERROR "ENABLE_LOG=ON but GALAY_SPDLOG_TARGET is not set. Check top-level spdlog configuration.")
    endif()
endif()

# 链接平台特定库
if(PLATFORM_LIBS)
    target_link_libraries(galay-kernel PUBLIC ${PLATFORM_LIBS})
endif()

# 设置库的编译选项
target_compile_options(galay-kernel PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 设置库的属性
set_target_properties(galay-kernel PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME galay-kernel
)

# 实验性 C++23 模块门面（import galay.kernel）
if(GALAY_KERNEL_CPP23_MODULES_EFFECTIVE)
    add_library(galay-kernel-modules STATIC)
    target_link_libraries(galay-kernel-modules PUBLIC galay-kernel)

    target_include_directories(galay-kernel-modules
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include/galay-kernel>
    )

    target_sources(galay-kernel-modules
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/module
            FILES ${CMAKE_CURRENT_SOURCE_DIR}/module/galay.kernel.cppm
    )
    set_target_properties(galay-kernel-modules PROPERTIES CXX_SCAN_FOR_MODULES ON)

    set_target_properties(galay-kernel-modules PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME galay-kernel-modules
    )
endif()

# 安装库文件
install(TARGETS galay-kernel
    EXPORT galay-kernel-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

if(GALAY_KERNEL_CPP23_MODULES_EFFECTIVE)
    install(TARGETS galay-kernel-modules
        EXPORT galay-kernel-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        FILE_SET CXX_MODULES DESTINATION include/galay-kernel/module
        INCLUDES DESTINATION include
    )
endif()

# 安装头文件（保留目录结构）
if(GALAY_KERNEL_CPP23_MODULES_EFFECTIVE)
    install(DIRECTORY async common kernel concurrency protocol module
        DESTINATION include/galay-kernel
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "*.inl"
    )
else()
    install(DIRECTORY async common kernel concurrency protocol module
        DESTINATION include/galay-kernel
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "*.inl"
        PATTERN "*.cppm"
    )
endif()

# 安装配置文件
include(CMakePackageConfigHelpers)

# 生成版本文件
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/galay-kernel-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# 生成配置文件
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/galay-kernel-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/galay-kernel-config.cmake"
    INSTALL_DESTINATION lib/cmake/galay-kernel
    PATH_VARS
        CMAKE_INSTALL_PREFIX
)

# 安装配置文件
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/galay-kernel-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/galay-kernel-config-version.cmake"
    DESTINATION lib/cmake/galay-kernel
)

# 安装导出目标
install(EXPORT galay-kernel-targets
    FILE galay-kernel-targets.cmake
    NAMESPACE galay-kernel::
    DESTINATION lib/cmake/galay-kernel
)
