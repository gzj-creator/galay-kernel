# Benchmark 可执行文件

# 压测服务器
add_executable(bench_server bench_server.cc)
target_link_libraries(bench_server PRIVATE galay-kernel)
target_include_directories(bench_server PRIVATE ${CMAKE_SOURCE_DIR})

# 压测客户端
add_executable(bench_client bench_client.cc)
target_link_libraries(bench_client PRIVATE galay-kernel)
target_include_directories(bench_client PRIVATE ${CMAKE_SOURCE_DIR})

# 文件 IO 压测
add_executable(bench_file_io bench_file_io.cc)
target_link_libraries(bench_file_io PRIVATE galay-kernel)
target_include_directories(bench_file_io PRIVATE ${CMAKE_SOURCE_DIR})
