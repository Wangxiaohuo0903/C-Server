# Server-12 Docker测试脚本 (Windows PowerShell)

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "  Server-12 Docker测试" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""

# 检查Docker是否运行
Write-Host "[1/6] 检查Docker..." -ForegroundColor Yellow
docker ps > $null 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Docker未运行，请启动Docker Desktop" -ForegroundColor Red
    exit 1
}
Write-Host "✓ Docker运行正常" -ForegroundColor Green
Write-Host ""

# 创建必要的目录
Write-Host "[2/6] 创建目录..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path "models" | Out-Null
New-Item -ItemType Directory -Force -Path "data" | Out-Null
Write-Host "✓ 目录创建完成" -ForegroundColor Green
Write-Host ""

# 下载模型（如果不存在）
Write-Host "[3/6] 检查模型文件..." -ForegroundColor Yellow
$modelPath = "models/tinyllama-q4.gguf"
if (Test-Path $modelPath) {
    Write-Host "✓ 模型文件已存在" -ForegroundColor Green
} else {
    Write-Host "⚠ 模型文件不存在" -ForegroundColor Yellow
    Write-Host "请下载模型到: $modelPath" -ForegroundColor Yellow
    Write-Host "下载地址: https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "或者运行:" -ForegroundColor Yellow
    Write-Host "  wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf -O $modelPath" -ForegroundColor Yellow
    Write-Host ""
    $response = Read-Host "继续构建（不包含模型测试）? (y/n)"
    if ($response -ne "y") {
        exit 0
    }
}
Write-Host ""

# 构建Docker镜像
Write-Host "[4/6] 构建Docker镜像..." -ForegroundColor Yellow
docker-compose build
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ 构建失败" -ForegroundColor Red
    exit 1
}
Write-Host "✓ 镜像构建成功" -ForegroundColor Green
Write-Host ""

# 启动容器
Write-Host "[5/6] 启动容器..." -ForegroundColor Yellow
docker-compose up -d
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ 启动失败" -ForegroundColor Red
    exit 1
}
Write-Host "✓ 容器启动成功" -ForegroundColor Green
Write-Host ""

# 等待服务就绪
Write-Host "[6/6] 等待服务就绪..." -ForegroundColor Yellow
Start-Sleep -Seconds 5

# 检查容器状态
docker ps --filter "name=server12-chat" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
Write-Host ""

# 查看日志
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "  容器日志（最近20行）" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
docker logs server12-chat --tail 20
Write-Host ""

# 运行API测试
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "  开始API测试" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""

$baseUrl = "http://localhost:8080"

# 测试1: 创建会话
Write-Host "Test 1: 创建会话" -ForegroundColor Yellow
$response = Invoke-RestMethod -Uri "$baseUrl/chat/create" -Method Post
Write-Host "Response: $($response | ConvertTo-Json -Compress)" -ForegroundColor White
if ($response.success -eq $true) {
    Write-Host "✓ Pass" -ForegroundColor Green
    $sessionId = $response.session_id
} else {
    Write-Host "✗ Fail" -ForegroundColor Red
}
Write-Host ""

# 测试2: 第一轮对话
if ($sessionId) {
    Write-Host "Test 2: 第一轮对话" -ForegroundColor Yellow
    $body = @{
        session_id = $sessionId
        message = "My name is Alice."
        max_tokens = "64"
    } | ConvertTo-Json
    
    try {
        $response = Invoke-RestMethod -Uri "$baseUrl/chat" -Method Post -Body $body -ContentType "application/json"
        Write-Host "Response: $($response | ConvertTo-Json -Compress)" -ForegroundColor White
        if ($response.success -eq $true) {
            Write-Host "✓ Pass" -ForegroundColor Green
        } else {
            Write-Host "✗ Fail" -ForegroundColor Red
        }
    } catch {
        Write-Host "✗ Fail - $($_.Exception.Message)" -ForegroundColor Red
    }
    Write-Host ""

    # 测试3: 第二轮对话（验证记忆）
    Write-Host "Test 3: 第二轮对话（验证记忆）" -ForegroundColor Yellow
    $body = @{
        session_id = $sessionId
        message = "What is my name?"
        max_tokens = "64"
    } | ConvertTo-Json
    
    try {
        $response = Invoke-RestMethod -Uri "$baseUrl/chat" -Method Post -Body $body -ContentType "application/json"
        Write-Host "Response: $($response | ConvertTo-Json -Compress)" -ForegroundColor White
        if ($response.success -eq $true) {
            Write-Host "✓ Pass - 模型应该记住名字是Alice" -ForegroundColor Green
        } else {
            Write-Host "✗ Fail" -ForegroundColor Red
        }
    } catch {
        Write-Host "✗ Fail - $($_.Exception.Message)" -ForegroundColor Red
    }
    Write-Host ""

    # 测试4: 获取历史
    Write-Host "Test 4: 获取会话历史" -ForegroundColor Yellow
    try {
        $response = Invoke-RestMethod -Uri "$baseUrl/chat/history?session_id=$sessionId" -Method Get
        Write-Host "Message count: $($response.message_count)" -ForegroundColor White
        if ($response.success -eq $true) {
            Write-Host "✓ Pass" -ForegroundColor Green
        } else {
            Write-Host "✗ Fail" -ForegroundColor Red
        }
    } catch {
        Write-Host "✗ Fail - $($_.Exception.Message)" -ForegroundColor Red
    }
    Write-Host ""
}

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "  测试完成" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "有用的命令:" -ForegroundColor Yellow
Write-Host "  查看日志: docker logs server12-chat -f" -ForegroundColor White
Write-Host "  停止服务: docker-compose down" -ForegroundColor White
Write-Host "  重启服务: docker-compose restart" -ForegroundColor White
Write-Host "  进入容器: docker exec -it server12-chat bash" -ForegroundColor White
Write-Host ""
