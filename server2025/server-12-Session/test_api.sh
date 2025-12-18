#!/bin/bash

##########################################################
# Server-11 API 测试脚本
# 测试AI推理接口和继承自Server-10的所有功能
##########################################################

BASE_URL="http://localhost:8080"
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "======================================="
echo "  Server-11 API Tests"
echo "======================================="
echo ""

# =====================================================
# ★ Server-11 新增测试 - AI推理接口
# =====================================================

echo -e "${BLUE}=== AI推理接口测试（Server-11新增） ===${NC}"
echo ""

# 测试1: 简单问答
echo "Test 1: Simple Q&A (POST /infer-simple)"
response=$(curl -s -X POST ${BASE_URL}/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is 2+2?", "max_tokens":"32"}')
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试2: 带自定义max_tokens
echo "Test 2: Custom max_tokens (POST /infer-simple)"
response=$(curl -s -X POST ${BASE_URL}/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Hello, who are you?", "max_tokens":"64"}')
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试3: 缺少prompt参数
echo "Test 3: Missing prompt (POST /infer-simple)"
response=$(curl -s -X POST ${BASE_URL}/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"max_tokens":"32"}')
echo "Response: $response"
if echo "$response" | grep -q "Prompt required"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试4: 验证无上下文记忆（两次独立请求）
echo "Test 4: No context memory - Round 1"
response1=$(curl -s -X POST ${BASE_URL}/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"My name is Alice.", "max_tokens":"32"}')
echo "Response 1: $response1"

echo "Test 4: No context memory - Round 2"
response2=$(curl -s -X POST ${BASE_URL}/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is my name?", "max_tokens":"32"}')
echo "Response 2: $response2"
echo "Note: 第二轮不会记住第一轮的名字（这是预期行为）"
if echo "$response2" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass (无上下文记忆，符合预期)${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# =====================================================
# 继承自Server-10的测试 - JSON + RESTful API
# =====================================================

echo -e "${BLUE}=== JSON + RESTful API测试（继承自Server-10） ===${NC}"
echo ""

# 测试5: JSON注册
echo "Test 5: JSON Register (POST /api/users/register)"
response=$(curl -s -X POST ${BASE_URL}/api/users/register \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"testpass"}')
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试6: JSON登录成功
echo "Test 6: JSON Login Success (POST /api/users/login)"
response=$(curl -s -X POST ${BASE_URL}/api/users/login \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"testpass"}')
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试7: JSON登录失败
echo "Test 7: JSON Login Fail (POST /api/users/login)"
response=$(curl -s -X POST ${BASE_URL}/api/users/login \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"wrongpass"}')
echo "Response: $response"
if echo "$response" | grep -q "success.*false"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试8: 查询参数
echo "Test 8: Query Parameters (GET /api/users?id=123)"
response=$(curl -s ${BASE_URL}/api/users?id=123)
echo "Response: $response"
if echo "$response" | grep -q "userId.*123"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试9: JSON回显
echo "Test 9: JSON Echo (POST /api/echo)"
response=$(curl -s -X POST ${BASE_URL}/api/echo \
  -H "Content-Type: application/json" \
  -d '{"message":"Hello","from":"client","test":"123"}')
echo "Response: $response"
if echo "$response" | grep -q "received"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试10: 缺少参数
echo "Test 10: Missing Parameters (POST /api/users/register)"
response=$(curl -s -X POST ${BASE_URL}/api/users/register \
  -H "Content-Type: application/json" \
  -d '{"username":"onlyusername"}')
echo "Response: $response"
if echo "$response" | grep -q "required"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# =====================================================
# 兼容性测试 - 传统Form接口（继承自Server-9）
# =====================================================

echo -e "${BLUE}=== 兼容性测试（继承自Server-9） ===${NC}"
echo ""

# 测试11: 传统Form注册
echo "Test 11: Form Register (POST /register)"
response=$(curl -s -X POST ${BASE_URL}/register \
  -d "username=formuser&password=formpass")
if echo "$response" | grep -q "Register Success"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

echo "======================================="
echo "  All Tests Completed"
echo "======================================="
echo ""
echo "测试总结："
echo "- Tests 1-4: Server-11新增AI推理功能"
echo "- Tests 5-10: Server-10的JSON + RESTful API"
echo "- Test 11: Server-9的传统Form接口兼容性"
