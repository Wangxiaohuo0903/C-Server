#!/bin/bash

# Server-13 Docker 测试脚本
# 用途：在 Docker 环境中测试 Context Pool 和 Batch Inference 功能

set -e

echo "========================================"
echo "Server-13 Docker Test Script"
echo "========================================"
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 步骤1：检查模型文件
echo -e "${YELLOW}Step 1: Checking model file...${NC}"
MODEL_DIR="../../AI-infra/models"
MODEL_FILE="smollm-360m-q4.gguf"

if [ ! -f "$MODEL_DIR/$MODEL_FILE" ]; then
    echo -e "${RED}Error: Model file not found at $MODEL_DIR/$MODEL_FILE${NC}"
    echo "Please download the model first:"
    echo "  wget https://huggingface.co/HuggingFaceTB/SmolLM-360M-Instruct-GGUF/resolve/main/smollm-360m-q4_k_m.gguf -O $MODEL_DIR/$MODEL_FILE"
    exit 1
fi

echo -e "${GREEN}✓ Model file found: $MODEL_DIR/$MODEL_FILE${NC}"
echo ""

# 步骤2：构建Docker镜像
echo -e "${YELLOW}Step 2: Building Docker image...${NC}"
docker build -f Dockerfile.server13 -t server13:latest .

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Docker image built successfully${NC}"
else
    echo -e "${RED}✗ Docker build failed${NC}"
    exit 1
fi
echo ""

# 步骤3：运行测试程序
echo -e "${YELLOW}Step 3: Running test program in Docker...${NC}"
echo "This will test:"
echo "  - KV Cache Reuse (SessionContextPool)"
echo "  - Batch Inference (BatchInferenceEngine)"
echo "  - Multi-session Concurrent Access"
echo ""

docker run --rm \
    -v "$(pwd)/../../AI-infra/models:/app/models:ro" \
    -e MODEL_PATH=/app/models/$MODEL_FILE \
    -e OMP_NUM_THREADS=4 \
    server13:latest \
    ./test_context_pool

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ All tests passed!${NC}"
else
    echo ""
    echo -e "${RED}✗ Tests failed${NC}"
    exit 1
fi
echo ""

# 步骤4：启动服务器（可选）
echo -e "${YELLOW}Step 4: Starting Server-13 (optional)...${NC}"
read -p "Do you want to start the Server-13 web server? (y/n) " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Starting Server-13 on http://localhost:6060 ..."
    docker-compose -f docker-compose.server13.yml up -d server13

    echo ""
    echo -e "${GREEN}✓ Server-13 started successfully!${NC}"
    echo ""
    echo "Access the server at:"
    echo "  - Web UI: http://localhost:6060/multichat.html"
    echo "  - API: http://localhost:6060/api/sessions"
    echo ""
    echo "To view logs:"
    echo "  docker logs -f server13_contextpool"
    echo ""
    echo "To stop the server:"
    echo "  docker-compose -f docker-compose.server13.yml down"
else
    echo "Server not started."
fi

echo ""
echo "========================================"
echo "Docker test completed!"
echo "========================================"
