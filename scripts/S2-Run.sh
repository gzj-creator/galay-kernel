#!/bin/bash

# 测试运行脚本
# 自动运行所有单元测试并生成结果文件

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_DIR="$BUILD_DIR/bin"
RESULTS_DIR="$BUILD_DIR/test_results"

echo "=========================================="
echo "Galay Kernel 测试运行脚本"
echo "=========================================="
echo ""

# 检查 build 目录
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}错误: build 目录不存在${NC}"
    echo "请先运行 cmake 和 make 编译项目"
    exit 1
fi

# 检查 bin 目录
if [ ! -d "$BIN_DIR" ]; then
    echo -e "${RED}错误: bin 目录不存在${NC}"
    echo "请先运行 make 编译测试"
    exit 1
fi

# 创建结果目录
mkdir -p "$RESULTS_DIR"

# 清理旧的测试结果
echo "清理旧的测试结果..."
rm -f "$RESULTS_DIR"/*.result
echo ""

# 定义测试列表（不需要配对的独立测试）
STANDALONE_TESTS=(
    "T11-ComputeScheduler"
    "T8-FileIo"
    "T9-FileWatcher"
    "T10-Timeout"
    "T12-MixedScheduler"
    "T13-AsyncMutex"
    "T14-MpscChannel"
    "T15-TimingWheel"
    "T17-UnsafeChannel"
    "T18-TimerScheduler"
)

# 定义需要 client-server 配对的测试
# 格式: "server_name:client_name:description"
CS_TESTS=(
    "T3-TcpServer:T4-TcpClient:TCP Echo"
    "T6-UdpServer:T7-UdpClient:UDP Echo"
)

# 运行独立测试
echo "=========================================="
echo "运行独立测试"
echo "=========================================="
echo ""

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

for test in "${STANDALONE_TESTS[@]}"; do
    TEST_BIN="$BIN_DIR/$test"

    if [ ! -f "$TEST_BIN" ]; then
        echo -e "${YELLOW}跳过: $test (可执行文件不存在)${NC}"
        continue
    fi

    echo "运行: $test"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    # 运行测试
    if "$TEST_BIN" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ $test 完成${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        EXIT_CODE=$?
        echo -e "${RED}✗ $test 失败 (退出码: $EXIT_CODE)${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    echo ""
done

# 运行 Client-Server 配对测试
echo "=========================================="
echo "运行 Client-Server 配对测试"
echo "=========================================="
echo ""

for cs_test in "${CS_TESTS[@]}"; do
    IFS=':' read -r server client desc <<< "$cs_test"

    SERVER_BIN="$BIN_DIR/$server"
    CLIENT_BIN="$BIN_DIR/$client"

    if [ ! -f "$SERVER_BIN" ] || [ ! -f "$CLIENT_BIN" ]; then
        echo -e "${YELLOW}跳过: $desc (可执行文件不存在)${NC}"
        continue
    fi

    echo "运行: $desc ($server + $client)"
    TOTAL_TESTS=$((TOTAL_TESTS + 2))

    # 启动服务器（后台运行）
    "$SERVER_BIN" > /dev/null 2>&1 &
    SERVER_PID=$!

    # 等待服务器启动
    sleep 1

    # 运行客户端
    CLIENT_SUCCESS=false
    if "$CLIENT_BIN" > /dev/null 2>&1; then
        CLIENT_SUCCESS=true
    fi

    # 等待服务器完成或超时
    SERVER_SUCCESS=false
    if wait $SERVER_PID 2>/dev/null; then
        SERVER_SUCCESS=true
    else
        # 如果服务器还在运行，杀掉它
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi

    # 检查结果
    if $SERVER_SUCCESS && $CLIENT_SUCCESS; then
        echo -e "${GREEN}✓ $desc 完成${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 2))
    else
        if ! $SERVER_SUCCESS; then
            echo -e "${RED}✗ $server 失败${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        else
            PASSED_TESTS=$((PASSED_TESTS + 1))
        fi

        if ! $CLIENT_SUCCESS; then
            echo -e "${RED}✗ $client 失败${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        else
            PASSED_TESTS=$((PASSED_TESTS + 1))
        fi
    fi
    echo ""
done

# 输出总结
echo "=========================================="
echo "测试运行完成"
echo "=========================================="
echo ""
echo "总测试数: $TOTAL_TESTS"
echo -e "${GREEN}通过: $PASSED_TESTS${NC}"
echo -e "${RED}失败: $FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}所有测试通过！${NC}"
    exit 0
else
    echo -e "${RED}有测试失败，请检查日志${NC}"
    exit 1
fi
