# 部署和执行协议测试脚本
# 此脚本用于将测试文件复制到目标主机并执行测试

# 目标主机配置
$TargetHost = "10.0.5.210"
$Username = "root"
$Password = "123"
$RemotePath = "/root/"
$SerialPort = "/dev/ttyS4"

# 本地文件路径
$LocalScript = "D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\debug_tools\protocol_test.py"
$LocalResults = "D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\test_results.json"
$LocalReport = "D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\test_report.md"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "协议测试部署脚本" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 步骤1: 检查本地文件
Write-Host "[步骤1] 检查本地文件..." -ForegroundColor Yellow
if (-not (Test-Path $LocalScript)) {
    Write-Host "错误: 找不到测试脚本文件: $LocalScript" -ForegroundColor Red
    exit 1
}
Write-Host "✓ 本地文件检查通过" -ForegroundColor Green
Write-Host ""

# 步骤2: 使用SCP复制文件到目标主机
Write-Host "[步骤2] 复制测试脚本到目标主机..." -ForegroundColor Yellow
Write-Host "提示: 需要输入密码 '$Password'" -ForegroundColor DarkYellow

# 使用pscp（如果可用）或scp
$SCPCommand = "scp -o StrictHostKeyChecking=no `"$LocalScript`" ${Username}@${TargetHost}:${RemotePath}"
Write-Host "执行命令: $SCPCommand" -ForegroundColor Gray

$SCPResult = cmd /c $SCPCommand 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "警告: SCP可能失败，尝试使用SSH直接传输..." -ForegroundColor Yellow
    
    # 备用方案：使用SSH和base64编码传输
    $ScriptContent = Get-Content $LocalScript -Raw -Encoding UTF8
    $EncodedScript = [Convert]::ToBase64String([System.Text.Encoding]::UTF8.GetBytes($ScriptContent))
    
    $SSHCommand = "ssh -o StrictHostKeyChecking=no ${Username}@${TargetHost} `"echo '${EncodedScript}' | base64 -d > ${RemotePath}protocol_test.py`""
    Write-Host "执行命令: $SSHCommand" -ForegroundColor Gray
    $SSHResult = cmd /c $SSHCommand 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "错误: 无法复制文件到目标主机" -ForegroundColor Red
        Write-Host "请手动执行以下步骤:" -ForegroundColor Yellow
        Write-Host "1. 复制 $LocalScript 到 $TargetHost:${RemotePath}" -ForegroundColor Yellow
        Write-Host "2. SSH登录到目标主机: ssh $Username@$TargetHost" -ForegroundColor Yellow
        Write-Host "3. 执行测试: cd ${RemotePath}; python3 protocol_test.py" -ForegroundColor Yellow
        exit 1
    }
}
Write-Host "✓ 文件复制完成" -ForegroundColor Green
Write-Host ""

# 步骤3: 在目标主机上执行测试
Write-Host "[步骤3] 在目标主机上执行测试..." -ForegroundColor Yellow
Write-Host "提示: 需要输入密码 '$Password'" -ForegroundColor DarkYellow

$TestCommand = "ssh -o StrictHostKeyChecking=no ${Username}@${TargetHost} `"cd ${RemotePath} && python3 protocol_test.py`""
Write-Host "执行命令: $TestCommand" -ForegroundColor Gray

$TestOutput = cmd /c $TestCommand 2>&1
Write-Host $TestOutput

if ($LASTEXITCODE -ne 0) {
    Write-Host "警告: 测试执行可能遇到问题" -ForegroundColor Yellow
} else {
    Write-Host "✓ 测试执行完成" -ForegroundColor Green
}
Write-Host ""

# 步骤4: 从目标主机复制测试结果
Write-Host "[步骤4] 复制测试结果到本地..." -ForegroundColor Yellow

# 复制JSON结果
$JSONRemotePath = "${RemotePath}test_results.json"
$JSONCommand = "scp -o StrictHostKeyChecking=no ${Username}@${TargetHost}:${JSONRemotePath} `"$LocalResults`""
Write-Host "执行命令: $JSONCommand" -ForegroundColor Gray

$JSONResult = cmd /c $JSONCommand 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ JSON结果已复制: $LocalResults" -ForegroundColor Green
} else {
    Write-Host "警告: 无法复制JSON结果" -ForegroundColor Yellow
}

# 复制Markdown报告
$MDRemotePath = "${RemotePath}test_report.md"
$MDCommand = "scp -o StrictHostKeyChecking=no ${Username}@${TargetHost}:${MDRemotePath} `"$LocalReport`""
Write-Host "执行命令: $MDCommand" -ForegroundColor Gray

$MDResult = cmd /c $MDCommand 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Markdown报告已复制: $LocalReport" -ForegroundColor Green
} else {
    Write-Host "警告: 无法复制Markdown报告" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "部署和测试完成" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 显示摘要
if (Test-Path $LocalResults) {
    Write-Host "测试结果文件:" -ForegroundColor Green
    Write-Host "  - JSON: $LocalResults" -ForegroundColor Gray
}
if (Test-Path $LocalReport) {
    Write-Host "  - Markdown: $LocalReport" -ForegroundColor Gray
}