@echo off
echo ========================================
echo 下载DeepSeek-R1-Distill-Qwen-1.5B模型
echo 大小: 约1GB
echo ========================================
echo.

cd /d "%~dp0"

echo 开始下载...
echo 如果下载失败，请尝试开启代理或使用浏览器下载
echo.

curl -L -o DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf ^
  "https://hf-mirror.com/unsloth/DeepSeek-R1-Distill-Qwen-1.5B-GGUF/resolve/main/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo 下载成功！
    echo 文件位置: %CD%\DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf
    echo ========================================
    echo.
    echo 接下来修改docker-compose.yml:
    echo MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf
    echo.
) else (
    echo.
    echo ========================================
    echo 下载失败！
    echo 请尝试:
    echo 1. 开启网络代理
    echo 2. 使用浏览器直接下载
    echo ========================================
)

pause
