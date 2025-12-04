# ============================================================
# 编译并测试推测式解码 (PowerShell 版本)
# ============================================================

$ErrorActionPreference = "Stop"

Write-Host "🔨 Building Speculative Decoding Test..." -ForegroundColor Green
Write-Host ""

# 进入项目目录
$ProjectDir = Join-Path $PSScriptRoot "..\AI-chats-linux"
Set-Location $ProjectDir

# 创建 build 目录
if (!(Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Set-Location "build"

# CMake 配置
Write-Host "⚙️  Configuring CMake..." -ForegroundColor Cyan
cmake .. -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ CMake configuration failed" -ForegroundColor Red
    exit 1
}

# 编译
Write-Host "🏗️  Compiling..." -ForegroundColor Cyan
cmake --build . --config Release -j 4

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Compilation failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "✅ Build complete!" -ForegroundColor Green
Write-Host ""

# 检查模型是否存在
$DraftModel = "..\models\draft\tinyllama-160m-q4.gguf"
$TargetModel = "..\models\tinyllama-1.1b-q4.gguf"

if (!(Test-Path $DraftModel)) {
    Write-Host "❌ Draft model not found: $DraftModel" -ForegroundColor Red
    Write-Host "Please run: scripts\download_draft_models.ps1" -ForegroundColor Yellow
    exit 1
}

if (!(Test-Path $TargetModel)) {
    Write-Host "⚠️  Target model not found: $TargetModel" -ForegroundColor Yellow
    Write-Host "Please download TinyLlama-1.1B model first" -ForegroundColor Yellow
    exit 1
}

Write-Host "📊 Running test..." -ForegroundColor Cyan
Write-Host ""

# 运行测试
& ".\Release\test_speculative.exe" `
    --model $TargetModel `
    --model-draft $DraftModel `
    --prompt "用Python实现快速排序算法" `
    --n-predict 100 `
    --n-draft 16 `
    --temperature 0.7 `
    --verbose

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Test failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "🎉 Test complete!" -ForegroundColor Green
