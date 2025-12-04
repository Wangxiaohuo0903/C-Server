@echo off
REM Batch Size修复验证 - Windows启动脚本
REM 会在WSL环境中运行Linux测试脚本

echo ╔══════════════════════════════════════════════════════════╗
echo ║     Batch Size修复 - 本地测试 (Windows启动器)          ║
echo ╚══════════════════════════════════════════════════════════╝
echo.

echo 📋 此脚本将:
echo   1. 在WSL中清理旧的编译文件
echo   2. 重新编译test_task_aware (包含batch size修复)
echo   3. 运行完整测试
echo   4. 验证修复效果
echo.
echo ⏱️  预计运行时间: 3-5分钟
echo.

pause

echo.
echo 🚀 启动WSL测试...
echo.

REM 获取脚本所在目录的Linux路径
set "SCRIPT_DIR=%~dp0"
set "LINUX_PATH=/mnt/c/Users/实习生/Documents/Code/server/C-Server/AI-infra/scripts/test_batch_fix.sh"

REM 在WSL中运行测试脚本
wsl bash "%LINUX_PATH%"

echo.
echo ════════════════════════════════════════════════════════════
echo   测试完成！
echo ════════════════════════════════════════════════════════════
echo.
echo 📄 查看日志文件:
wsl ls -lh /mnt/c/Users/实习生/Documents/Code/server/C-Server/AI-infra/BATCH_FIX_VERIFIED_*.log
echo.

pause
