# Benchmark 可执行文件

# B1 - ComputeScheduler 压测
add_executable(B1-ComputeScheduler b1_compute.cc)
target_link_libraries(B1-ComputeScheduler PRIVATE galay-kernel)
target_include_directories(B1-ComputeScheduler PRIVATE ${CMAKE_SOURCE_DIR})

# B2 - TCP 压测服务器
add_executable(B2-TcpServer b2_tcp.cc)
target_link_libraries(B2-TcpServer PRIVATE galay-kernel)
target_include_directories(B2-TcpServer PRIVATE ${CMAKE_SOURCE_DIR})

# B3 - TCP 压测客户端
add_executable(B3-TcpClient b3_tcp.cc)
target_link_libraries(B3-TcpClient PRIVATE galay-kernel)
target_include_directories(B3-TcpClient PRIVATE ${CMAKE_SOURCE_DIR})

# B4 - UDP 压测服务器
add_executable(B4-UdpServer b4_udp.cc)
target_link_libraries(B4-UdpServer PRIVATE galay-kernel)
target_include_directories(B4-UdpServer PRIVATE ${CMAKE_SOURCE_DIR})

# B5 - UDP 压测客户端
add_executable(B5-UdpClient b5_udp.cc)
target_link_libraries(B5-UdpClient PRIVATE galay-kernel)
target_include_directories(B5-UdpClient PRIVATE ${CMAKE_SOURCE_DIR})

# B6 - UDP Socket 压测（原始合并版本）
add_executable(B6-Udp b6_udp.cc)
target_link_libraries(B6-Udp PRIVATE galay-kernel)
target_include_directories(B6-Udp PRIVATE ${CMAKE_SOURCE_DIR})

# B7 - 文件 IO 压测
add_executable(B7-FileIo b7_fileio.cc)
target_link_libraries(B7-FileIo PRIVATE galay-kernel)
target_include_directories(B7-FileIo PRIVATE ${CMAKE_SOURCE_DIR})

# B8 - MpscChannel 压测
add_executable(B8-MpscChannel b8_mpsc.cc)
target_link_libraries(B8-MpscChannel PRIVATE galay-kernel)
target_include_directories(B8-MpscChannel PRIVATE ${CMAKE_SOURCE_DIR})

# B9 - UnsafeChannel 压测
add_executable(B9-UnsafeChannel b9_unsafe.cc)
target_link_libraries(B9-UnsafeChannel PRIVATE galay-kernel)
target_include_directories(B9-UnsafeChannel PRIVATE ${CMAKE_SOURCE_DIR})

# B10 - RingBuffer 压测
add_executable(B10-Ringbuffer b10_ringbuffer.cc)
target_link_libraries(B10-Ringbuffer PRIVATE galay-kernel)
target_include_directories(B10-Ringbuffer PRIVATE ${CMAKE_SOURCE_DIR})

# B11 - TCP IOV 压测服务器 (readv/writev)
add_executable(B11-TcpIovServer b11_iov.cc)
target_link_libraries(B11-TcpIovServer PRIVATE galay-kernel)
target_include_directories(B11-TcpIovServer PRIVATE ${CMAKE_SOURCE_DIR})

# B12 - TCP IOV 压测客户端 (readv/writev)
add_executable(B12-TcpIovClient b12_iov.cc)
target_link_libraries(B12-TcpIovClient PRIVATE galay-kernel)
target_include_directories(B12-TcpIovClient PRIVATE ${CMAKE_SOURCE_DIR})

# B13 - SendFile 性能对比和压测
add_executable(B13-Sendfile b13_sendfile.cc)
target_link_libraries(B13-Sendfile PRIVATE galay-kernel)
target_include_directories(B13-Sendfile PRIVATE ${CMAKE_SOURCE_DIR})

# B14 - Scheduler injected wakeup 压测
add_executable(B14-SchedulerInjectedWakeup b14_wakeup.cc)
target_link_libraries(B14-SchedulerInjectedWakeup PRIVATE galay-kernel)
target_include_directories(B14-SchedulerInjectedWakeup PRIVATE ${CMAKE_SOURCE_DIR})
