#!/bin/bash

# 测试结果检查脚本
# 汇总所有 test_result_writer 写入的结果文件，检查是否全部通过

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="$PROJECT_ROOT/test_results"

echo "=========================================="
echo "Galay Kernel 测试结果检查"
echo "=========================================="
echo ""

# 检查结果目录
if [ ! -d "$RESULTS_DIR" ]; then
    echo -e "${RED}错误: 测试结果目录不存在: $RESULTS_DIR${NC}"
    echo "请先运行 scripts/run.sh 执行测试"
    exit 1
fi

# 查找所有 .result 文件
RESULT_FILES=("$RESULTS_DIR"/*.result)

if [ ! -e "${RESULT_FILES[0]}" ]; then
    echo -e "${RED}错误: 未找到测试结果文件${NC}"
    echo "请先运行 scripts/run.sh 执行测试"
    exit 1
fi

# 统计变量
TOTAL_TESTS=0
TOTAL_PASSED=0
TOTAL_FAILED=0
FAILED_CASES=()

echo "检查测试结果文件..."
echo ""

# 遍历所有结果文件
for result_file in "${RESULT_FILES[@]}"; do
    if [ ! -f "$result_file" ]; then
        continue
    fi

    # 读取结果文件
    TEST_NAME=""
    TOTAL=0
    PASSED=0
    FAILED=0
    STATUS=""
    DURATION=0

    while IFS='=' read -r key value; do
        case "$key" in
            TEST_NAME)
                TEST_NAME="$value"
                ;;
            TOTAL)
                TOTAL="$value"
                ;;
            PASSED)
                PASSED="$value"
                ;;
            FAILED)
                FAILED="$value"
                ;;
            STATUS)
                STATUS="$value"
                ;;
            DURATION_MS)
                DURATION="$value"
                ;;
        esac
    done < "$result_file"

    # 累加统计
    TOTAL_TESTS=$((TOTAL_TESTS + TOTAL))
    TOTAL_PASSED=$((TOTAL_PASSED + PASSED))
    TOTAL_FAILED=$((TOTAL_FAILED + FAILED))

    # 输出单个测试结果
    if [ "$STATUS" = "PASS" ]; then
        echo -e "${GREEN}✓${NC} $TEST_NAME: ${GREEN}PASS${NC} (${PASSED}/${TOTAL}, ${DURATION}ms)"
    else
        echo -e "${RED}✗${NC} $TEST_NAME: ${RED}FAIL${NC} (${PASSED}/${TOTAL}, ${FAILED} failed, ${DURATION}ms)"
        FAILED_CASES+=("$TEST_NAME (文件: $(basename "$result_file"))")
    fi
done

echo ""
echo "=========================================="
echo "测试结果汇总"
echo "=========================================="
echo ""
echo "总测试用例数: $TOTAL_TESTS"
echo -e "${GREEN}通过: $TOTAL_PASSED${NC}"
echo -e "${RED}失败: $TOTAL_FAILED${NC}"

if [ ${#FAILED_CASES[@]} -gt 0 ]; then
    echo ""
    echo -e "${RED}失败的测试:${NC}"
    for failed_case in "${FAILED_CASES[@]}"; do
        echo -e "  ${RED}✗${NC} $failed_case"
    done
fi

echo ""
echo "=========================================="

# 返回状态
if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "${GREEN}所有测试通过！✓${NC}"
    exit 0
else
    echo -e "${RED}有 $TOTAL_FAILED 个测试失败 ✗${NC}"
    exit 1
fi
