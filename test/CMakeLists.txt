# 测试可执行文件

# TcpSocket测试
add_executable(test_tcp_socket test_tcp_socket.cc)
target_link_libraries(test_tcp_socket PRIVATE galay-kernel)
target_include_directories(test_tcp_socket PRIVATE ${CMAKE_SOURCE_DIR})

# UdpSocket测试
add_executable(test_udp_socket test_udp_socket.cc)
target_link_libraries(test_udp_socket PRIVATE galay-kernel)
target_include_directories(test_udp_socket PRIVATE ${CMAKE_SOURCE_DIR})

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
