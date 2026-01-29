# 测试可执行文件

# T1 - CoroutineChain测试
add_executable(T1-CoroutineChain T1-CoroutineChain.cc)
target_link_libraries(T1-CoroutineChain PRIVATE galay-kernel)
target_include_directories(T1-CoroutineChain PRIVATE ${CMAKE_SOURCE_DIR})

# T2 - TcpSocket测试 (旧版本，包含 client 和 server)
add_executable(T2-TcpSocket T2-TcpSocket.cc)
target_link_libraries(T2-TcpSocket PRIVATE galay-kernel)
target_include_directories(T2-TcpSocket PRIVATE ${CMAKE_SOURCE_DIR})

# T3 - TCP Server测试 (独立)
add_executable(T3-TcpServer T3-TcpServer.cc)
target_link_libraries(T3-TcpServer PRIVATE galay-kernel)
target_include_directories(T3-TcpServer PRIVATE ${CMAKE_SOURCE_DIR})

# T4 - TCP Client测试 (独立)
add_executable(T4-TcpClient T4-TcpClient.cc)
target_link_libraries(T4-TcpClient PRIVATE galay-kernel)
target_include_directories(T4-TcpClient PRIVATE ${CMAKE_SOURCE_DIR})

# T5 - UdpSocket测试 (旧版本，包含 client 和 server)
add_executable(T5-UdpSocket T5-UdpSocket.cc)
target_link_libraries(T5-UdpSocket PRIVATE galay-kernel)
target_include_directories(T5-UdpSocket PRIVATE ${CMAKE_SOURCE_DIR})

# T6 - UDP Server测试 (独立)
add_executable(T6-UdpServer T6-UdpServer.cc)
target_link_libraries(T6-UdpServer PRIVATE galay-kernel)
target_include_directories(T6-UdpServer PRIVATE ${CMAKE_SOURCE_DIR})

# T7 - UDP Client测试 (独立)
add_executable(T7-UdpClient T7-UdpClient.cc)
target_link_libraries(T7-UdpClient PRIVATE galay-kernel)
target_include_directories(T7-UdpClient PRIVATE ${CMAKE_SOURCE_DIR})

# T8 - File IO测试
add_executable(T8-FileIo T8-FileIo.cc)
target_link_libraries(T8-FileIo PRIVATE galay-kernel)
target_include_directories(T8-FileIo PRIVATE ${CMAKE_SOURCE_DIR})

# T9 - FileWatcher测试
add_executable(T9-FileWatcher T9-FileWatcher.cc)
target_link_libraries(T9-FileWatcher PRIVATE galay-kernel)
target_include_directories(T9-FileWatcher PRIVATE ${CMAKE_SOURCE_DIR})

# T10 - Timeout测试
add_executable(T10-Timeout T10-Timeout.cc)
target_link_libraries(T10-Timeout PRIVATE galay-kernel)
target_include_directories(T10-Timeout PRIVATE ${CMAKE_SOURCE_DIR})

# T11 - ComputeScheduler测试
add_executable(T11-ComputeScheduler T11-ComputeScheduler.cc)
target_link_libraries(T11-ComputeScheduler PRIVATE galay-kernel)
target_include_directories(T11-ComputeScheduler PRIVATE ${CMAKE_SOURCE_DIR})

# T12 - Mixed Scheduler测试 (IOScheduler + ComputeScheduler)
add_executable(T12-MixedScheduler T12-MixedScheduler.cc)
target_link_libraries(T12-MixedScheduler PRIVATE galay-kernel)
target_include_directories(T12-MixedScheduler PRIVATE ${CMAKE_SOURCE_DIR})

# T13 - AsyncMutex测试
add_executable(T13-AsyncMutex T13-AsyncMutex.cc)
target_link_libraries(T13-AsyncMutex PRIVATE galay-kernel)
target_include_directories(T13-AsyncMutex PRIVATE ${CMAKE_SOURCE_DIR})

# T14 - MpscChannel测试
add_executable(T14-MpscChannel T14-MpscChannel.cc)
target_link_libraries(T14-MpscChannel PRIVATE galay-kernel)
target_include_directories(T14-MpscChannel PRIVATE ${CMAKE_SOURCE_DIR})

# T15 - TimingWheel测试
add_executable(T15-TimingWheel T15-TimingWheel.cc)
target_link_libraries(T15-TimingWheel PRIVATE galay-kernel)
target_include_directories(T15-TimingWheel PRIVATE ${CMAKE_SOURCE_DIR})

# T16 - NotifyAwaitable测试 (RecvNotify/SendNotify)
add_executable(T16-NotifyAwaitable T16-NotifyAwaitable.cc)
target_link_libraries(T16-NotifyAwaitable PRIVATE galay-kernel)
target_include_directories(T16-NotifyAwaitable PRIVATE ${CMAKE_SOURCE_DIR})

# T17 - UnsafeChannel测试
add_executable(T17-UnsafeChannel T17-UnsafeChannel.cc)
target_link_libraries(T17-UnsafeChannel PRIVATE galay-kernel)
target_include_directories(T17-UnsafeChannel PRIVATE ${CMAKE_SOURCE_DIR})

# T18 - TimerScheduler多线程测试
add_executable(T18-TimerScheduler T18-TimerScheduler.cc)
target_link_libraries(T18-TimerScheduler PRIVATE galay-kernel)
target_include_directories(T18-TimerScheduler PRIVATE ${CMAKE_SOURCE_DIR})

# T19 - readv/writev测试
add_executable(T19-ReadvWritev T19-ReadvWritev.cc)
target_link_libraries(T19-ReadvWritev PRIVATE galay-kernel)
target_include_directories(T19-ReadvWritev PRIVATE ${CMAKE_SOURCE_DIR})

# T20 - RingBuffer + readv/writev 集成测试
add_executable(T20-RingbufferIo T20-RingbufferIo.cc)
target_link_libraries(T20-RingbufferIo PRIVATE galay-kernel)
target_include_directories(T20-RingbufferIo PRIVATE ${CMAKE_SOURCE_DIR})

# T21 - Runtime测试 (管理多个调度器)
add_executable(T21-Runtime T21-Runtime.cc)
target_link_libraries(T21-Runtime PRIVATE galay-kernel)
target_include_directories(T21-Runtime PRIVATE ${CMAKE_SOURCE_DIR})

# T22 - SendFile测试 (零拷贝文件传输)
add_executable(T22-Sendfile T22-Sendfile.cc)
target_link_libraries(T22-Sendfile PRIVATE galay-kernel)
target_include_directories(T22-Sendfile PRIVATE ${CMAKE_SOURCE_DIR})

# T23 - SendFile基础功能测试
add_executable(T23-SendfileBasic T23-SendfileBasic.cc)
target_link_libraries(T23-SendfileBasic PRIVATE galay-kernel)
target_include_directories(T23-SendfileBasic PRIVATE ${CMAKE_SOURCE_DIR})

# T24 - Spawn函数测试
add_executable(T24-Spawn T24-Spawn.cc)
target_link_libraries(T24-Spawn PRIVATE galay-kernel)
target_include_directories(T24-Spawn PRIVATE ${CMAKE_SOURCE_DIR})

# T25 - Spawn函数简单演示
add_executable(T25-SpawnSimple T25-SpawnSimple.cc)
target_link_libraries(T25-SpawnSimple PRIVATE galay-kernel)
target_include_directories(T25-SpawnSimple PRIVATE ${CMAKE_SOURCE_DIR})