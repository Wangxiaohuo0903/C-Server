#!/bin/bash

echo "========================================="
echo "  Server-12 代码验证脚本"
echo "========================================="
echo ""

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0

# 检查文件是否存在
echo -e "${YELLOW}[1/5] 检查必需文件...${NC}"
files=("SessionManager.h" "Router.h" "HttpServer.h" "main.cpp" "SimpleInference.h" "test_chat.sh")
for file in "${files[@]}"; do
    if [ -f "$file" ]; then
        echo -e "${GREEN}✓${NC} $file 存在"
        ((PASS_COUNT++))
    else
        echo -e "${RED}✗${NC} $file 缺失"
        ((FAIL_COUNT++))
    fi
done
echo ""

# 检查SessionManager关键类定义
echo -e "${YELLOW}[2/5] 检查SessionManager类定义...${NC}"
if grep -q "class SessionManager" SessionManager.h; then
    echo -e "${GREEN}✓${NC} SessionManager类已定义"
    ((PASS_COUNT++))
else
    echo -e "${RED}✗${NC} SessionManager类未定义"
    ((FAIL_COUNT++))
fi

if grep -q "struct Message" SessionManager.h; then
    echo -e "${GREEN}✓${NC} Message结构已定义"
    ((PASS_COUNT++))
else
    echo -e "${RED}✗${NC} Message结构未定义"
    ((FAIL_COUNT++))
fi

if grep -q "struct Session" SessionManager.h; then
    echo -e "${GREEN}✓${NC} Session结构已定义"
    ((PASS_COUNT++))
else
    echo -e "${RED}✗${NC} Session结构未定义"
    ((FAIL_COUNT++))
fi
echo ""

# 检查关键方法
echo -e "${YELLOW}[3/5] 检查关键方法...${NC}"
methods=("createSession" "addUserMessage" "addAssistantMessage" "getRecentContext" "deleteSession")
for method in "${methods[@]}"; do
    if grep -q "$method" SessionManager.h; then
        echo -e "${GREEN}✓${NC} $method 方法已实现"
        ((PASS_COUNT++))
    else
        echo -e "${RED}✗${NC} $method 方法缺失"
        ((FAIL_COUNT++))
    fi
done
echo ""

# 检查新的API接口
echo -e "${YELLOW}[4/5] 检查API接口定义...${NC}"
apis=("/chat/create" "/chat" "/chat/history" "/chat/delete")
for api in "${apis[@]}"; do
    if grep -q "\"$api\"" Router.h; then
        echo -e "${GREEN}✓${NC} $api 接口已定义"
        ((PASS_COUNT++))
    else
        echo -e "${RED}✗${NC} $api 接口缺失"
        ((FAIL_COUNT++))
    fi
done
echo ""

# 检查main.cpp集成
echo -e "${YELLOW}[5/5] 检查main.cpp集成...${NC}"
if grep -q "SessionManager" main.cpp; then
    echo -e "${GREEN}✓${NC} SessionManager已包含"
    ((PASS_COUNT++))
else
    echo -e "${RED}✗${NC} SessionManager未包含"
    ((FAIL_COUNT++))
fi

if grep -q "setupRESTfulRoutes.*inference.*sessionMgr" main.cpp; then
    echo -e "${GREEN}✓${NC} setupRESTfulRoutes参数正确"
    ((PASS_COUNT++))
else
    echo -e "${RED}✗${NC} setupRESTfulRoutes参数不正确"
    ((FAIL_COUNT++))
fi
echo ""

# 总结
echo "========================================="
echo -e "  验证结果: ${GREEN}$PASS_COUNT 通过${NC} | ${RED}$FAIL_COUNT 失败${NC}"
echo "========================================="
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}✅ 所有检查通过！代码结构完整。${NC}"
    echo ""
    echo "下一步："
    echo "1. 在Linux/WSL环境编译"
    echo "2. 准备llama.cpp库和GGUF模型"
    echo "3. 运行 ./test_chat.sh 进行功能测试"
else
    echo -e "${RED}❌ 发现 $FAIL_COUNT 个问题，请检查代码。${NC}"
fi
