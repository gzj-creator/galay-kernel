# Benchmark 可执行文件

# TCP 压测服务器
add_executable(bench_tcp_server bench_tcp_server.cc)
target_link_libraries(bench_tcp_server PRIVATE galay-kernel)
target_include_directories(bench_tcp_server PRIVATE ${CMAKE_SOURCE_DIR})

# TCP 压测客户端
add_executable(bench_tcp_client bench_tcp_client.cc)
target_link_libraries(bench_tcp_client PRIVATE galay-kernel)
target_include_directories(bench_tcp_client PRIVATE ${CMAKE_SOURCE_DIR})

# UDP 压测服务器
add_executable(bench_udp_server bench_udp_server.cc)
target_link_libraries(bench_udp_server PRIVATE galay-kernel)
target_include_directories(bench_udp_server PRIVATE ${CMAKE_SOURCE_DIR})

# UDP 压测客户端
add_executable(bench_udp_client bench_udp_client.cc)
target_link_libraries(bench_udp_client PRIVATE galay-kernel)
target_include_directories(bench_udp_client PRIVATE ${CMAKE_SOURCE_DIR})

# UDP Socket 压测（原始合并版本）
add_executable(bench_udp bench_udp.cc)
target_link_libraries(bench_udp PRIVATE galay-kernel)
target_include_directories(bench_udp PRIVATE ${CMAKE_SOURCE_DIR})

# 文件 IO 压测
add_executable(bench_file_io bench_file_io.cc)
target_link_libraries(bench_file_io PRIVATE galay-kernel)
target_include_directories(bench_file_io PRIVATE ${CMAKE_SOURCE_DIR})

# ComputeScheduler 压测
add_executable(bench_compute_scheduler bench_compute_scheduler.cc)
target_link_libraries(bench_compute_scheduler PRIVATE galay-kernel)
target_include_directories(bench_compute_scheduler PRIVATE ${CMAKE_SOURCE_DIR})

# MpscChannel 压测
add_executable(bench_mpsc_channel bench_mpsc_channel.cc)
target_link_libraries(bench_mpsc_channel PRIVATE galay-kernel)
target_include_directories(bench_mpsc_channel PRIVATE ${CMAKE_SOURCE_DIR})

# UnsafeChannel 压测
add_executable(bench_unsafe_channel bench_unsafe_channel.cc)
target_link_libraries(bench_unsafe_channel PRIVATE galay-kernel)
target_include_directories(bench_unsafe_channel PRIVATE ${CMAKE_SOURCE_DIR})
