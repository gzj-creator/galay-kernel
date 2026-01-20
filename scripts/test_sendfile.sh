#!/bin/bash
# SendFile 测试和压测脚本

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  SendFile 测试和压测套件${NC}"
echo -e "${BLUE}========================================${NC}\n"

# 检查构建目录
if [ ! -d "build" ]; then
    echo -e "${YELLOW}创建 build 目录...${NC}"
    mkdir build
fi

cd build

# 配置和编译
echo -e "\n${YELLOW}[1/4] 配置项目...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

echo -e "\n${YELLOW}[2/4] 编译测试程序...${NC}"
make test_sendfile_basic bench_sendfile -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ 编译失败${NC}"
    exit 1
fi

echo -e "${GREEN}✓ 编译成功${NC}"

# 运行基础功能测试
echo -e "\n${YELLOW}[3/4] 运行基础功能测试...${NC}"
echo -e "${BLUE}========================================${NC}"
./bin/test_sendfile_basic
BASIC_RESULT=$?

if [ $BASIC_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ 基础功能测试通过${NC}"
else
    echo -e "${RED}✗ 基础功能测试失败${NC}"
fi

# 运行性能对比和压测
echo -e "\n${YELLOW}[4/4] 运行性能对比和压测...${NC}"
echo -e "${BLUE}========================================${NC}"
./bin/bench_sendfile
BENCHMARK_RESULT=$?

if [ $BENCHMARK_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ 性能对比和压测完成${NC}"
else
    echo -e "${RED}✗ 性能对比和压测失败${NC}"
fi

# 汇总结果
echo -e "\n${BLUE}========================================${NC}"
echo -e "${BLUE}  测试结果汇总${NC}"
echo -e "${BLUE}========================================${NC}"

TOTAL_TESTS=2
PASSED_TESTS=0

if [ $BASIC_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ 基础功能测试: PASSED${NC}"
    ((PASSED_TESTS++))
else
    echo -e "${RED}✗ 基础功能测试: FAILED${NC}"
fi

if [ $BENCHMARK_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ 性能对比和压测: PASSED${NC}"
    ((PASSED_TESTS++))
else
    echo -e "${RED}✗ 性能对比和压测: FAILED${NC}"
fi

echo -e "${BLUE}========================================${NC}"
echo -e "总计: ${PASSED_TESTS}/${TOTAL_TESTS} 通过"

if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    echo -e "${GREEN}所有测试通过！${NC}"
    exit 0
else
    echo -e "${RED}部分测试失败${NC}"
    exit 1
fi
