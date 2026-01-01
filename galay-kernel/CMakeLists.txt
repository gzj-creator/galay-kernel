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

# 设置包含目录
target_include_directories(galay-kernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/galay-kernel>
)

# 链接spdlog
if(ENABLE_LOG)
    target_link_libraries(galay-kernel PUBLIC spdlog::spdlog)
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

# 安装库文件
install(TARGETS galay-kernel
    EXPORT galay-kernel-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# 安装头文件
install(FILES ${HEADERS}
    DESTINATION include/galay-kernel
)

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
