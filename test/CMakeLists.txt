# 测试可执行文件 - 自动扫描目录

# 扫描所有 T*.cc 测试文件
file(GLOB TEST_SOURCES "T*.cc")

# 为每个测试文件创建可执行文件
foreach(TEST_SOURCE ${TEST_SOURCES})
    # 获取文件名（不含路径和扩展名）
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

    # 创建可执行文件
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE galay-kernel)
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR})
endforeach()
