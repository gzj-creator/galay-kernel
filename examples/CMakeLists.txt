# 示例可执行文件

# E1 - Sendfile示例
add_executable(E1-SendfileExample E1-SendfileExample.cc)
target_link_libraries(E1-SendfileExample PRIVATE galay-kernel)
target_include_directories(E1-SendfileExample PRIVATE ${CMAKE_SOURCE_DIR})

# E2 - TCP Echo Server示例
add_executable(E2-TcpEchoServer E2-TcpEchoServer.cc)
target_link_libraries(E2-TcpEchoServer PRIVATE galay-kernel)
target_include_directories(E2-TcpEchoServer PRIVATE ${CMAKE_SOURCE_DIR})

# E3 - TCP Client示例
add_executable(E3-TcpClient E3-TcpClient.cc)
target_link_libraries(E3-TcpClient PRIVATE galay-kernel)
target_include_directories(E3-TcpClient PRIVATE ${CMAKE_SOURCE_DIR})

# E4 - Coroutine Basic示例
add_executable(E4-CoroutineBasic E4-CoroutineBasic.cc)
target_link_libraries(E4-CoroutineBasic PRIVATE galay-kernel)
target_include_directories(E4-CoroutineBasic PRIVATE ${CMAKE_SOURCE_DIR})

# E5 - UDP Echo示例
add_executable(E5-UdpEcho E5-UdpEcho.cc)
target_link_libraries(E5-UdpEcho PRIVATE galay-kernel)
target_include_directories(E5-UdpEcho PRIVATE ${CMAKE_SOURCE_DIR})
