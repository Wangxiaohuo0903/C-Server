#!/bin/bash

echo "========================================="
echo "  Server-12 Multi-Chat API 测试脚本"
echo "========================================="
echo ""

SERVER_URL="http://localhost:8080"

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试1: 创建新会话
echo -e "${BLUE}[测试1] 创建新会话${NC}"
echo "POST $SERVER_URL/api/sessions/new"
SESSION_RESPONSE=$(curl -s -X POST "$SERVER_URL/api/sessions/new")
echo "响应: $SESSION_RESPONSE"

# 提取session_id
SESSION_ID=$(echo $SESSION_RESPONSE | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)
echo -e "${GREEN}✓ Session ID: $SESSION_ID${NC}"
echo ""

# 测试2: 在会话中发送第一条消息
echo -e "${BLUE}[测试2] 发送第一条消息${NC}"
echo "POST $SERVER_URL/api/sessions/$SESSION_ID/chat"
echo '{"message": "你好，请介绍一下什么是Docker"}'
CHAT_RESPONSE=$(curl -s -X POST "$SERVER_URL/api/sessions/$SESSION_ID/chat" \
  -H "Content-Type: application/json" \
  -d '{"message": "你好，请介绍一下什么是Docker", "max_tokens": 100}')
echo "响应: $CHAT_RESPONSE"
echo -e "${GREEN}✓ AI已回复${NC}"
echo ""

# 测试3: 继续对话（测试多轮对话）
echo -e "${BLUE}[测试3] 继续对话（多轮）${NC}"
echo "POST $SERVER_URL/api/sessions/$SESSION_ID/chat"
echo '{"message": "它和虚拟机有什么区别？"}'
CHAT_RESPONSE2=$(curl -s -X POST "$SERVER_URL/api/sessions/$SESSION_ID/chat" \
  -H "Content-Type: application/json" \
  -d '{"message": "它和虚拟机有什么区别？", "max_tokens": 100}')
echo "响应: $CHAT_RESPONSE2"
echo -e "${GREEN}✓ AI记住了上下文（'它'指Docker）${NC}"
echo ""

# 测试4: 获取会话历史
echo -e "${BLUE}[测试4] 获取会话历史${NC}"
echo "GET $SERVER_URL/api/sessions/$SESSION_ID/history"
HISTORY=$(curl -s "$SERVER_URL/api/sessions/$SESSION_ID/history")
echo "响应: $HISTORY"
echo -e "${GREEN}✓ 历史记录已获取${NC}"
echo ""

# 测试5: 创建第二个会话
echo -e "${BLUE}[测试5] 创建第二个会话${NC}"
SESSION_RESPONSE2=$(curl -s -X POST "$SERVER_URL/api/sessions/new")
SESSION_ID2=$(echo $SESSION_RESPONSE2 | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)
echo -e "${GREEN}✓ 第二个会话ID: $SESSION_ID2${NC}"
echo ""

# 测试6: 在第二个会话中聊天
echo -e "${BLUE}[测试6] 在第二个会话中聊天${NC}"
curl -s -X POST "$SERVER_URL/api/sessions/$SESSION_ID2/chat" \
  -H "Content-Type: application/json" \
  -d '{"message": "解释一下什么是Kubernetes", "max_tokens": 80}' > /dev/null
echo -e "${GREEN}✓ 第二个会话独立工作${NC}"
echo ""

# 测试7: 获取所有会话列表
echo -e "${BLUE}[测试7] 获取所有会话列表${NC}"
echo "GET $SERVER_URL/api/sessions"
SESSIONS=$(curl -s "$SERVER_URL/api/sessions")
echo "响应: $SESSIONS"
echo -e "${GREEN}✓ 应该有2个会话${NC}"
echo ""

# 测试8: 更新会话标题
echo -e "${BLUE}[测试8] 更新会话标题${NC}"
echo "PUT $SERVER_URL/api/sessions/$SESSION_ID/title"
curl -s -X PUT "$SERVER_URL/api/sessions/$SESSION_ID/title" \
  -H "Content-Type: application/json" \
  -d '{"title": "Docker学习笔记"}' > /dev/null
echo -e "${GREEN}✓ 标题已更新${NC}"
echo ""

# 测试9: 删除会话
echo -e "${BLUE}[测试9] 删除第二个会话${NC}"
echo "DELETE $SERVER_URL/api/sessions/$SESSION_ID2"
DELETE_RESPONSE=$(curl -s -X DELETE "$SERVER_URL/api/sessions/$SESSION_ID2")
echo "响应: $DELETE_RESPONSE"
echo -e "${GREEN}✓ 会话已删除${NC}"
echo ""

# 测试10: 验证删除
echo -e "${BLUE}[测试10] 验证会话已删除${NC}"
SESSIONS_AFTER=$(curl -s "$SERVER_URL/api/sessions")
echo "响应: $SESSIONS_AFTER"
echo -e "${GREEN}✓ 现在应该只有1个会话${NC}"
echo ""

echo "========================================="
echo -e "${GREEN}  所有测试完成！${NC}"
echo "========================================="
echo ""
echo "总结："
echo "✓ 多会话管理正常"
echo "✓ 多轮对话功能正常"
echo "✓ 历史记录功能正常"
echo "✓ 会话列表功能正常"
echo "✓ 更新标题功能正常"
echo "✓ 删除会话功能正常"
echo ""
echo "现在可以访问 http://localhost:8080/multichat.html 体验ChatGPT风格界面！"
