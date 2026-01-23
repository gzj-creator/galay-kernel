# 测试可执行文件

add_executable(test_coroutine_chain test_coroutine_chain.cc)
target_link_libraries(test_coroutine_chain PRIVATE galay-kernel)
target_include_directories(test_coroutine_chain PRIVATE ${CMAKE_SOURCE_DIR})

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

# NotifyAwaitable测试 (RecvNotify/SendNotify)
add_executable(test_notify_awaitable test_notify_awaitable.cc)
target_link_libraries(test_notify_awaitable PRIVATE galay-kernel)
target_include_directories(test_notify_awaitable PRIVATE ${CMAKE_SOURCE_DIR})

# UnsafeChannel测试
add_executable(test_unsafe_channel test_unsafe_channel.cc)
target_link_libraries(test_unsafe_channel PRIVATE galay-kernel)
target_include_directories(test_unsafe_channel PRIVATE ${CMAKE_SOURCE_DIR})

# TimerScheduler多线程测试
add_executable(test_timer_scheduler test_timer_scheduler.cc)
target_link_libraries(test_timer_scheduler PRIVATE galay-kernel)
target_include_directories(test_timer_scheduler PRIVATE ${CMAKE_SOURCE_DIR})

# readv/writev测试
add_executable(test_readv_writev test_readv_writev.cc)
target_link_libraries(test_readv_writev PRIVATE galay-kernel)
target_include_directories(test_readv_writev PRIVATE ${CMAKE_SOURCE_DIR})

# RingBuffer + readv/writev 集成测试
add_executable(test_ringbuffer_io test_ringbuffer_io.cc)
target_link_libraries(test_ringbuffer_io PRIVATE galay-kernel)
target_include_directories(test_ringbuffer_io PRIVATE ${CMAKE_SOURCE_DIR})

# Runtime测试 (管理多个调度器)
add_executable(test_runtime test_runtime.cc)
target_link_libraries(test_runtime PRIVATE galay-kernel)
target_include_directories(test_runtime PRIVATE ${CMAKE_SOURCE_DIR})

# SendFile测试 (零拷贝文件传输)
add_executable(test_sendfile test_sendfile.cc)
target_link_libraries(test_sendfile PRIVATE galay-kernel)
target_include_directories(test_sendfile PRIVATE ${CMAKE_SOURCE_DIR})

# SendFile基础功能测试
add_executable(test_sendfile_basic test_sendfile_basic.cc)
target_link_libraries(test_sendfile_basic PRIVATE galay-kernel)
target_include_directories(test_sendfile_basic PRIVATE ${CMAKE_SOURCE_DIR})

# Spawn函数测试
add_executable(test_spawn test_spawn.cc)
target_link_libraries(test_spawn PRIVATE galay-kernel)
target_include_directories(test_spawn PRIVATE ${CMAKE_SOURCE_DIR})

# Spawn函数简单演示
add_executable(test_spawn_simple test_spawn_simple.cc)
target_link_libraries(test_spawn_simple PRIVATE galay-kernel)
target_include_directories(test_spawn_simple PRIVATE ${CMAKE_SOURCE_DIR})