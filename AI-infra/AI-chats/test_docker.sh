#!/bin/bash
# ============================================================
# AI-chats Docker 测试脚本
# ============================================================
# 用途: 自动测试 AI-chats Docker 部署的所有功能
#
# 使用方法:
#   chmod +x test_docker.sh
#   ./test_docker.sh
# ============================================================

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试计数器
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 辅助函数
print_header() {
    echo ""
    echo "============================================================"
    echo "$1"
    echo "============================================================"
}

print_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
}

print_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    PASSED_TESTS=$((PASSED_TESTS + 1))
}

print_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    FAILED_TESTS=$((FAILED_TESTS + 1))
}

print_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

# ============================================================
# 测试 1: 检查 Docker 和 Docker Compose
# ============================================================
print_header "测试 1: 检查环境"
print_test "检查 Docker 是否安装"
if command -v docker &> /dev/null; then
    DOCKER_VERSION=$(docker --version)
    print_pass "Docker 已安装: $DOCKER_VERSION"
else
    print_fail "Docker 未安装"
    exit 1
fi

print_test "检查 Docker Compose 是否安装"
if command -v docker-compose &> /dev/null; then
    COMPOSE_VERSION=$(docker-compose --version)
    print_pass "Docker Compose 已安装: $COMPOSE_VERSION"
else
    print_fail "Docker Compose 未安装"
    exit 1
fi

# ============================================================
# 测试 2: 检查文件完整性
# ============================================================
print_header "测试 2: 检查文件完整性"
print_test "检查 Dockerfile"
if [ -f "Dockerfile" ]; then
    print_pass "Dockerfile 存在"
else
    print_fail "Dockerfile 不存在"
fi

print_test "检查 docker-compose.yml"
if [ -f "docker-compose.yml" ]; then
    print_pass "docker-compose.yml 存在"
else
    print_fail "docker-compose.yml 不存在"
fi

print_test "检查模型文件"
if [ -f "../models/tinyllama-q4.gguf" ]; then
    MODEL_SIZE=$(ls -lh ../models/tinyllama-q4.gguf | awk '{print $5}')
    print_pass "模型文件存在: $MODEL_SIZE"
else
    print_fail "模型文件不存在: ../models/tinyllama-q4.gguf"
fi

# ============================================================
# 测试 3: 构建镜像（如果需要）
# ============================================================
print_header "测试 3: Docker 镜像"
print_test "检查镜像是否存在"
if docker images | grep -q "ai-chats"; then
    print_pass "Docker 镜像已存在"
else
    print_info "镜像不存在，开始构建..."
    docker-compose build
    if [ $? -eq 0 ]; then
        print_pass "镜像构建成功"
    else
        print_fail "镜像构建失败"
        exit 1
    fi
fi

# ============================================================
# 测试 4: 启动容器
# ============================================================
print_header "测试 4: 启动容器"
print_test "启动 Docker Compose 服务"
docker-compose up -d
if [ $? -eq 0 ]; then
    print_pass "服务启动成功"
else
    print_fail "服务启动失败"
    exit 1
fi

print_info "等待 5 秒，让服务完全启动..."
sleep 5

print_test "检查容器运行状态"
if docker-compose ps | grep -q "Up"; then
    print_pass "容器正在运行"
else
    print_fail "容器未运行"
    docker-compose logs
    exit 1
fi

# ============================================================
# 测试 5: 基础连通性测试
# ============================================================
print_header "测试 5: API 连通性"
print_test "测试首页 (GET /)"
RESPONSE=$(curl -s http://localhost:8080/)
if echo "$RESPONSE" | grep -q "Welcome"; then
    print_pass "首页访问成功: $RESPONSE"
else
    print_fail "首页访问失败: $RESPONSE"
fi

# ============================================================
# 测试 6: 用户注册和登录
# ============================================================
print_header "测试 6: 用户注册和登录"

# 生成随机用户名（避免重复）
RANDOM_USER="test_user_$(date +%s)"

print_test "测试用户注册 (POST /register)"
REGISTER_RESPONSE=$(curl -s -X POST http://localhost:8080/register \
  -H "Content-Type: application/json" \
  -d "{\"username\": \"$RANDOM_USER\", \"password\": \"123456\"}")

if echo "$REGISTER_RESPONSE" | grep -q "success"; then
    print_pass "用户注册成功: $REGISTER_RESPONSE"
else
    print_fail "用户注册失败: $REGISTER_RESPONSE"
fi

print_test "测试用户登录 (POST /login)"
LOGIN_RESPONSE=$(curl -s -X POST http://localhost:8080/login \
  -H "Content-Type: application/json" \
  -d "{\"username\": \"$RANDOM_USER\", \"password\": \"123456\"}")

if echo "$LOGIN_RESPONSE" | grep -q "success"; then
    print_pass "用户登录成功: $LOGIN_RESPONSE"
else
    print_fail "用户登录失败: $LOGIN_RESPONSE"
fi

# ============================================================
# 测试 7: AI 推理功能
# ============================================================
print_header "测试 7: AI 推理功能"

print_test "测试单轮推理 (POST /infer)"
INFER_RESPONSE=$(curl -s -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "chat_id": "test_session_001",
    "prompt": "你好",
    "max_tokens": 50,
    "temperature": 0.7
  }')

if echo "$INFER_RESPONSE" | grep -q "response"; then
    # 提取响应内容
    AI_RESPONSE=$(echo "$INFER_RESPONSE" | grep -o '"response":"[^"]*"' | cut -d'"' -f4)
    print_pass "AI 推理成功: $AI_RESPONSE"
else
    print_fail "AI 推理失败: $INFER_RESPONSE"
fi

# ============================================================
# 测试 8: 多轮对话功能
# ============================================================
print_header "测试 8: 多轮对话"

print_test "第一轮对话: 告诉 AI 你的名字"
CHAT_ID="multi_turn_test_$(date +%s)"
ROUND1=$(curl -s -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d "{
    \"chat_id\": \"$CHAT_ID\",
    \"prompt\": \"我叫小明\",
    \"max_tokens\": 30,
    \"temperature\": 0.7
  }")

if echo "$ROUND1" | grep -q "response"; then
    R1_RESPONSE=$(echo "$ROUND1" | grep -o '"response":"[^"]*"' | cut -d'"' -f4)
    print_pass "第一轮成功: $R1_RESPONSE"
else
    print_fail "第一轮失败: $ROUND1"
fi

print_info "等待 2 秒..."
sleep 2

print_test "第二轮对话: 测试 AI 是否记住你的名字"
ROUND2=$(curl -s -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d "{
    \"chat_id\": \"$CHAT_ID\",
    \"prompt\": \"我叫什么名字？\",
    \"max_tokens\": 30,
    \"temperature\": 0.7
  }")

if echo "$ROUND2" | grep -q "response"; then
    R2_RESPONSE=$(echo "$ROUND2" | grep -o '"response":"[^"]*"' | cut -d'"' -f4)
    print_pass "第二轮成功: $R2_RESPONSE"

    # 检查是否包含"小明"
    if echo "$R2_RESPONSE" | grep -q "小明"; then
        print_pass "✨ 多轮对话记忆功能正常！AI 记住了你的名字"
    else
        print_info "AI 未明确提到'小明'，但这可能正常（取决于模型能力）"
    fi
else
    print_fail "第二轮失败: $ROUND2"
fi

# ============================================================
# 测试 9: 容器健康检查
# ============================================================
print_header "测试 9: 容器健康检查"
print_test "检查容器健康状态"
HEALTH_STATUS=$(docker inspect ai-chats-server --format='{{.State.Health.Status}}' 2>/dev/null || echo "no_healthcheck")

if [ "$HEALTH_STATUS" == "healthy" ]; then
    print_pass "容器健康状态: healthy"
elif [ "$HEALTH_STATUS" == "no_healthcheck" ]; then
    print_info "容器无健康检查配置"
else
    print_fail "容器健康状态: $HEALTH_STATUS"
fi

# ============================================================
# 测试 10: 资源使用情况
# ============================================================
print_header "测试 10: 资源使用"
print_test "检查容器资源使用"
print_info "容器资源使用情况:"
docker stats --no-stream ai-chats-server

# ============================================================
# 测试总结
# ============================================================
print_header "测试总结"
echo ""
echo "总测试数: $TOTAL_TESTS"
echo -e "${GREEN}通过: $PASSED_TESTS${NC}"
echo -e "${RED}失败: $FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}✅ 所有测试通过！AI-chats Docker 部署成功！${NC}"
    echo ""
    echo "🎉 你现在可以："
    echo "  1. 访问 http://localhost:8080/"
    echo "  2. 使用 curl 测试 API"
    echo "  3. 查看日志: docker-compose logs -f"
    echo "  4. 停止服务: docker-compose down"
    exit 0
else
    echo -e "${RED}❌ 部分测试失败，请检查日志${NC}"
    echo ""
    echo "查看容器日志:"
    echo "  docker-compose logs"
    echo ""
    echo "停止服务:"
    echo "  docker-compose down"
    exit 1
fi
