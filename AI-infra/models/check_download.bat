@echo off
:loop
cls
echo ========================================
echo DeepSeek-R1 模型下载进度
echo ========================================
echo.
dir DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf 2>nul
if %ERRORLEVEL% EQU 0 (
    echo.
    echo 目标大小: 约 1,000 MB
    echo 下载完成判断: 文件大小达到 900MB+ 即可使用
) else (
    echo 文件还未创建，等待下载开始...
)
echo.
echo 每10秒自动刷新...
echo 按 Ctrl+C 退出监控
timeout /t 10 >nul
goto loop
