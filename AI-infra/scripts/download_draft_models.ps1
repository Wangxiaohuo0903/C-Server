# ============================================================
# Draft 模型下载脚本 (PowerShell 版本)
# 用于推测式解码（Speculative Decoding）
# ============================================================

$ErrorActionPreference = "Stop"

$ModelsDir = "..\models\draft"
New-Item -ItemType Directory -Force -Path $ModelsDir | Out-Null

Write-Host "📦 Starting draft model download..." -ForegroundColor Green
Write-Host "📁 Target directory: $ModelsDir" -ForegroundColor Cyan
Write-Host ""

# ============================================================
# 显示菜单
# ============================================================

function Show-Menu {
    Write-Host "请选择要下载的 draft 模型：" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "1. TinyLlama-160M (推荐) - 极致速度，适合 CPU 推理" -ForegroundColor White
    Write-Host "   参数量: 160M" -ForegroundColor Gray
    Write-Host "   文件大小: ~100MB (Q4_K_M 量化)" -ForegroundColor Gray
    Write-Host "   适用场景: 快速 draft，接受率 40-60%" -ForegroundColor Gray
    Write-Host ""
    Write-Host "2. TinyLlama-500M - 平衡速度与准确性" -ForegroundColor White
    Write-Host "   参数量: 500M" -ForegroundColor Gray
    Write-Host "   文件大小: ~300MB (Q4_K_M 量化)" -ForegroundColor Gray
    Write-Host "   适用场景: 更高接受率，略慢" -ForegroundColor Gray
    Write-Host ""
    Write-Host "3. SmolLM-135M - 超小模型，极低延迟" -ForegroundColor White
    Write-Host "   参数量: 135M" -ForegroundColor Gray
    Write-Host "   文件大小: ~80MB (Q4_K_M 量化)" -ForegroundColor Gray
    Write-Host "   适用场景: 资源受限环境" -ForegroundColor Gray
    Write-Host ""
    Write-Host "4. 下载全部模型（用于对比测试）" -ForegroundColor White
    Write-Host ""
    Write-Host "0. 退出" -ForegroundColor Red
    Write-Host ""
    $choice = Read-Host "请输入选项 [0-4]"
    return $choice
}

# ============================================================
# 下载函数
# ============================================================

function Download-Model {
    param(
        [string]$Name,
        [string]$Url,
        [string]$Filename
    )

    Write-Host ""
    Write-Host "⬇️  Downloading $Name..." -ForegroundColor Green
    Write-Host "🔗 URL: $Url" -ForegroundColor Cyan
    Write-Host "📄 Filename: $Filename" -ForegroundColor Cyan
    Write-Host ""

    $FilePath = Join-Path $ModelsDir $Filename

    if (Test-Path $FilePath) {
        Write-Host "⚠️  File already exists: $FilePath" -ForegroundColor Yellow
        $overwrite = Read-Host "是否覆盖? [y/N]"
        if ($overwrite -notmatch "^[Yy]$") {
            Write-Host "⏭️  Skipped." -ForegroundColor Gray
            return
        }
    }

    try {
        Write-Host "正在下载..." -ForegroundColor Cyan
        $ProgressPreference = 'SilentlyContinue'  # 加速下载
        Invoke-WebRequest -Uri $Url -OutFile $FilePath -UseBasicParsing

        if (Test-Path $FilePath) {
            $FileSize = (Get-Item $FilePath).Length / 1MB
            Write-Host "✅ Downloaded: $FilePath" -ForegroundColor Green
            Write-Host "📊 File size: $([math]::Round($FileSize, 2)) MB" -ForegroundColor Cyan
        }
    }
    catch {
        Write-Host "❌ Download failed: $_" -ForegroundColor Red
        exit 1
    }
}

# ============================================================
# 模型下载函数
# ============================================================

function Download-TinyLlama160M {
    Download-Model `
        -Name "TinyLlama-160M" `
        -Url "https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF/resolve/main/tinyllama-160m-chat-v0.6.Q4_K_M.gguf" `
        -Filename "tinyllama-160m-q4.gguf"
}

function Download-TinyLlama500M {
    Download-Model `
        -Name "TinyLlama-500M" `
        -Url "https://huggingface.co/TinyLlama/TinyLlama-500M-Chat-v0.6-GGUF/resolve/main/tinyllama-500m-chat-v0.6.Q4_K_M.gguf" `
        -Filename "tinyllama-500m-q4.gguf"
}

function Download-SmolLM135M {
    Download-Model `
        -Name "SmolLM-135M" `
        -Url "https://huggingface.co/HuggingFaceTB/SmolLM-135M-Instruct-GGUF/resolve/main/smollm-135m-instruct.Q4_K_M.gguf" `
        -Filename "smollm-135m-q4.gguf"
}

# ============================================================
# 主流程
# ============================================================

$choice = Show-Menu

switch ($choice) {
    "1" {
        Download-TinyLlama160M
    }
    "2" {
        Download-TinyLlama500M
    }
    "3" {
        Download-SmolLM135M
    }
    "4" {
        Write-Host "📦 Downloading all models..." -ForegroundColor Green
        Download-TinyLlama160M
        Download-TinyLlama500M
        Download-SmolLM135M
    }
    "0" {
        Write-Host "👋 Bye!" -ForegroundColor Yellow
        exit 0
    }
    default {
        Write-Host "❌ Invalid option" -ForegroundColor Red
        exit 1
    }
}

# ============================================================
# 验证下载
# ============================================================

Write-Host ""
Write-Host "✅ Download complete!" -ForegroundColor Green
Write-Host ""
Write-Host "📂 Downloaded models:" -ForegroundColor Cyan
Get-ChildItem -Path $ModelsDir -File | ForEach-Object {
    $size = [math]::Round($_.Length / 1MB, 2)
    Write-Host "  $($_.Name) - $size MB" -ForegroundColor White
}
Write-Host ""
Write-Host "🚀 Next steps:" -ForegroundColor Yellow
Write-Host "1. 更新配置文件指定 draft 模型路径" -ForegroundColor White
Write-Host "2. 编译并测试 SpeculativeDecoder" -ForegroundColor White
Write-Host "3. 启动服务并使用推测式解码" -ForegroundColor White
Write-Host ""
