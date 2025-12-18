#!/bin/bash

##########################################################
# Server-10 API 测试脚本
# 测试JSON解析和RESTful API功能
##########################################################

BASE_URL="http://localhost:8080"
GREEN='\033[0.32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "======================================="
echo "  Server-10 API Tests"
echo "======================================="
echo ""

# 测试1: JSON注册
echo "Test 1: JSON Register (POST /api/users/register)"
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

# 测试2: JSON登录成功
echo "Test 2: JSON Login Success (POST /api/users/login)"
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

# 测试3: JSON登录失败
echo "Test 3: JSON Login Fail (POST /api/users/login)"
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

# 测试4: 查询参数
echo "Test 4: Query Parameters (GET /api/users?id=123)"
response=$(curl -s ${BASE_URL}/api/users?id=123)
echo "Response: $response"
if echo "$response" | grep -q "userId.*123"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试5: JSON回显
echo "Test 5: JSON Echo (POST /api/echo)"
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

# 测试6: 缺少参数
echo "Test 6: Missing Parameters (POST /api/users/register)"
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

# 测试7: 兼容性 - 传统Form注册
echo "Test 7: Form Register (POST /register)"
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
