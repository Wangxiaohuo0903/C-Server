# ============================================================
# 完整性能测试脚本
# 运行所有对比实验并生成报告
# ============================================================

$ErrorActionPreference = "Stop"

Write-Host "╔════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║       Speculative Decoding Full Benchmark Suite           ║" -ForegroundColor Cyan
Write-Host "╚════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ============================================================
# 配置
# ============================================================

$ProjectDir = Join-Path $PSScriptRoot "..\AI-chats-linux"
$ModelDir = Join-Path $PSScriptRoot "..\models"
$TargetModel = Join-Path $ModelDir "tinyllama-1.1b-q4.gguf"
$DraftModel = Join-Path $ModelDir "draft\tinyllama-160m-q4.gguf"
$OutputDir = Join-Path $PSScriptRoot "..\benchmark_results"
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

# 创建输出目录
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Write-Host "📁 Configuration:" -ForegroundColor Yellow
Write-Host "   Target Model:    $TargetModel"
Write-Host "   Draft Model:     $DraftModel"
Write-Host "   Output Dir:      $OutputDir"
Write-Host "   Timestamp:       $Timestamp"
Write-Host ""

# 检查模型文件
if (!(Test-Path $TargetModel)) {
    Write-Host "❌ Target model not found: $TargetModel" -ForegroundColor Red
    exit 1
}

if (!(Test-Path $DraftModel)) {
    Write-Host "❌ Draft model not found: $DraftModel" -ForegroundColor Red
    Write-Host "Please run: .\download_draft_models.ps1" -ForegroundColor Yellow
    exit 1
}

# ============================================================
# 编译项目
# ============================================================

Write-Host "🔨 Building project..." -ForegroundColor Green

Set-Location $ProjectDir

if (!(Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Set-Location "build"

cmake .. -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ CMake configuration failed" -ForegroundColor Red
    exit 1
}

cmake --build . --config Release -j 4
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Build failed" -ForegroundColor Red
    exit 1
}

Write-Host "✅ Build complete" -ForegroundColor Green
Write-Host ""

# ============================================================
# 运行基准测试
# ============================================================

Write-Host "🚀 Running benchmark tests..." -ForegroundColor Green
Write-Host ""

$BenchmarkExe = ".\Release\benchmark_comparison.exe"
$OutputFile = Join-Path $OutputDir "benchmark_$Timestamp.json"

if (!(Test-Path $BenchmarkExe)) {
    Write-Host "❌ Benchmark executable not found: $BenchmarkExe" -ForegroundColor Red
    exit 1
}

& $BenchmarkExe `
    --model $TargetModel `
    --model-draft $DraftModel `
    --output $OutputFile `
    --max-tokens 100 `
    --temperature 0.7

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Benchmark failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "✅ Benchmark complete!" -ForegroundColor Green
Write-Host "📄 Results saved to: $OutputFile" -ForegroundColor Cyan
Write-Host ""

# ============================================================
# 生成报告
# ============================================================

Write-Host "📊 Generating analysis report..." -ForegroundColor Green

$AnalysisScript = Join-Path $PSScriptRoot "analyze_benchmark.py"

if (Test-Path $AnalysisScript) {
    python $AnalysisScript $OutputFile

    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Analysis complete!" -ForegroundColor Green
    } else {
        Write-Host "⚠️  Analysis script failed (non-critical)" -ForegroundColor Yellow
    }
} else {
    Write-Host "⚠️  Analysis script not found: $AnalysisScript" -ForegroundColor Yellow
    Write-Host "   Skipping automatic analysis" -ForegroundColor Yellow
}

Write-Host ""

# ============================================================
# 总结
# ============================================================

Write-Host "╔════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║                    Benchmark Complete!                     ║" -ForegroundColor Green
Write-Host "╚════════════════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "📁 Output files:" -ForegroundColor Cyan
Write-Host "   Results (JSON):  $OutputFile"

$PngFile = $OutputFile -replace '\.json$', '_charts.png'
if (Test-Path $PngFile) {
    Write-Host "   Charts (PNG):    $PngFile"
}

$ReportFile = $OutputFile -replace '\.json$', '_report.md'
if (Test-Path $ReportFile) {
    Write-Host "   Report (MD):     $ReportFile"
}

Write-Host ""
Write-Host "🎯 Next steps:" -ForegroundColor Yellow
Write-Host "   1. Review the JSON results"
Write-Host "   2. Check the generated charts (if available)"
Write-Host "   3. Analyze performance metrics"
Write-Host "   4. Update your thesis with experimental data"
Write-Host ""
