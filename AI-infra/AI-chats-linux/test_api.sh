#!/bin/bash

# API 测试脚本
# 使用方法: ./test_api.sh

set -e

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SERVER="http://localhost:8080"

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}AI-Infra Server API 测试${NC}"
echo -e "${BLUE}================================${NC}"
echo ""

# 检查服务器是否运行
echo -e "${YELLOW}[1/5] 检查服务器状态...${NC}"
if curl -s --max-time 2 "${SERVER}/" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ 服务器运行中${NC}"
else
    echo -e "${RED}✗ 服务器未运行！${NC}"
    echo -e "${YELLOW}请先启动服务器:${NC}"
    echo "  cd build && ./ai_infra_server_mac"
    exit 1
fi
echo ""

# 测试根路由
echo -e "${YELLOW}[2/5] 测试根路由 GET /${NC}"
RESPONSE=$(curl -s "${SERVER}/")
if [ "$RESPONSE" == "Hello, World!" ]; then
    echo -e "${GREEN}✓ 响应正确: $RESPONSE${NC}"
else
    echo -e "${RED}✗ 响应异常: $RESPONSE${NC}"
fi
echo ""

# 测试用户注册
echo -e "${YELLOW}[3/5] 测试用户注册 POST /register${NC}"
USERNAME="test_user_$(date +%s)"
PASSWORD="test_pass_123"

RESPONSE=$(curl -s -X POST "${SERVER}/register" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}")

echo "请求: username=${USERNAME}, password=${PASSWORD}"
echo "响应: ${RESPONSE}"

if echo "$RESPONSE" | grep -q "success\|ok\|registered"; then
    echo -e "${GREEN}✓ 注册成功${NC}"
else
    echo -e "${YELLOW}⚠ 响应: $RESPONSE${NC}"
fi
echo ""

# 测试用户登录
echo -e "${YELLOW}[4/5] 测试用户登录 POST /login${NC}"
RESPONSE=$(curl -s -X POST "${SERVER}/login" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}")

echo "请求: username=${USERNAME}, password=${PASSWORD}"
echo "响应: ${RESPONSE}"

if echo "$RESPONSE" | grep -q "success\|ok\|login"; then
    echo -e "${GREEN}✓ 登录成功${NC}"
else
    echo -e "${YELLOW}⚠ 响应: $RESPONSE${NC}"
fi
echo ""

# 测试 AI 推理
echo -e "${YELLOW}[5/5] 测试 AI 推理 POST /infer${NC}"
echo -e "${BLUE}提示: 这可能需要几秒钟...${NC}"

CHAT_ID="test_session_$(date +%s)"
PROMPT="Hello, what is 2+2?"

RESPONSE=$(curl -s -X POST "${SERVER}/infer" \
  -H "Content-Type: application/json" \
  -d "{\"prompt\":\"${PROMPT}\",\"chat_id\":\"${CHAT_ID}\"}")

echo "请求: prompt=\"${PROMPT}\", chat_id=\"${CHAT_ID}\""
echo "响应: ${RESPONSE}"

if echo "$RESPONSE" | grep -q "answer"; then
    echo -e "${GREEN}✓ AI 推理成功${NC}"
    ANSWER=$(echo "$RESPONSE" | grep -o '"answer":"[^"]*"' | cut -d'"' -f4)
    echo -e "${GREEN}AI 回答: ${ANSWER}${NC}"
else
    echo -e "${RED}✗ AI 推理失败${NC}"
    echo -e "${YELLOW}可能原因:${NC}"
    echo "  1. 模型文件路径未配置或不存在"
    echo "  2. 内存不足"
    echo "  3. 模型加载失败"
fi
echo ""

# 测试会话重置
echo -e "${YELLOW}[额外] 测试会话重置 POST /reset${NC}"
RESPONSE=$(curl -s -X POST "${SERVER}/reset" \
  -H "Content-Type: application/json" \
  -d "{\"chat_id\":\"${CHAT_ID}\"}")

echo "请求: chat_id=\"${CHAT_ID}\""
echo "响应: ${RESPONSE}"

if echo "$RESPONSE" | grep -q "ok\|reset"; then
    echo -e "${GREEN}✓ 会话重置成功${NC}"
else
    echo -e "${YELLOW}⚠ 响应: $RESPONSE${NC}"
fi
echo ""

echo -e "${BLUE}================================${NC}"
echo -e "${GREEN}测试完成！${NC}"
echo -e "${BLUE}================================${NC}"
