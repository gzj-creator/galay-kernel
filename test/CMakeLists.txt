# 测试可执行文件

# TcpSocket测试 (旧版本，包含 client 和 server)
add_executable(test_tcp_socket test_tcp_socket.cc)
target_link_libraries(test_tcp_socket PRIVATE galay-kernel)
target_include_directories(test_tcp_socket PRIVATE ${CMAKE_SOURCE_DIR})

# TCP Server测试 (独立)
add_executable(test_tcp_server test_tcp_server.cc)
target_link_libraries(test_tcp_server PRIVATE galay-kernel)
target_include_directories(test_tcp_server PRIVATE ${CMAKE_SOURCE_DIR})

# TCP Client测试 (独立)
add_executable(test_tcp_client test_tcp_client.cc)
target_link_libraries(test_tcp_client PRIVATE galay-kernel)
target_include_directories(test_tcp_client PRIVATE ${CMAKE_SOURCE_DIR})

# UdpSocket测试 (旧版本，包含 client 和 server)
add_executable(test_udp_socket test_udp_socket.cc)
target_link_libraries(test_udp_socket PRIVATE galay-kernel)
target_include_directories(test_udp_socket PRIVATE ${CMAKE_SOURCE_DIR})

# UDP Server测试 (独立)
add_executable(test_udp_server test_udp_server.cc)
target_link_libraries(test_udp_server PRIVATE galay-kernel)
target_include_directories(test_udp_server PRIVATE ${CMAKE_SOURCE_DIR})

# UDP Client测试 (独立)
add_executable(test_udp_client test_udp_client.cc)
target_link_libraries(test_udp_client PRIVATE galay-kernel)
target_include_directories(test_udp_client PRIVATE ${CMAKE_SOURCE_DIR})

# File IO测试
add_executable(test_file_io test_file_io.cc)
target_link_libraries(test_file_io PRIVATE galay-kernel)
target_include_directories(test_file_io PRIVATE ${CMAKE_SOURCE_DIR})

# FileWatcher测试
add_executable(test_file_watcher test_file_watcher.cc)
target_link_libraries(test_file_watcher PRIVATE galay-kernel)
target_include_directories(test_file_watcher PRIVATE ${CMAKE_SOURCE_DIR})

# Timeout测试
add_executable(test_timeout test_timeout.cc)
target_link_libraries(test_timeout PRIVATE galay-kernel)
target_include_directories(test_timeout PRIVATE ${CMAKE_SOURCE_DIR})

# ComputeScheduler测试
add_executable(test_compute_scheduler test_compute_scheduler.cc)
target_link_libraries(test_compute_scheduler PRIVATE galay-kernel)
target_include_directories(test_compute_scheduler PRIVATE ${CMAKE_SOURCE_DIR})

# Mixed Scheduler测试 (IOScheduler + ComputeScheduler)
add_executable(test_mixed_scheduler test_mixed_scheduler.cc)
target_link_libraries(test_mixed_scheduler PRIVATE galay-kernel)
target_include_directories(test_mixed_scheduler PRIVATE ${CMAKE_SOURCE_DIR})

# AsyncMutex测试
add_executable(test_async_mutex test_async_mutex.cc)
target_link_libraries(test_async_mutex PRIVATE galay-kernel)
target_include_directories(test_async_mutex PRIVATE ${CMAKE_SOURCE_DIR})

# MpscChannel测试
add_executable(test_mpsc_channel test_mpsc_channel.cc)
target_link_libraries(test_mpsc_channel PRIVATE galay-kernel)
target_include_directories(test_mpsc_channel PRIVATE ${CMAKE_SOURCE_DIR})

# TimingWheel测试
add_executable(test_timing_wheel test_timing_wheel.cc)
target_link_libraries(test_timing_wheel PRIVATE galay-kernel)
target_include_directories(test_timing_wheel PRIVATE ${CMAKE_SOURCE_DIR})

