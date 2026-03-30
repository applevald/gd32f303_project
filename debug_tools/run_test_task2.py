#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
任务2：协议测试执行脚本
连接目标主机，上传测试脚本，执行测试，获取结果
"""

import paramiko
import os
import sys
import time
import json
from datetime import datetime

# 目标主机配置 - 使用备用测试主机
HOST = "10.0.5.204"
USERNAME = "root"
PASSWORD = "123"
REMOTE_PATH = "/root/"

# 本地文件路径
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOCAL_SCRIPT = os.path.join(BASE_DIR, "protocol_test.py")
LOCAL_RESULTS = os.path.join(os.path.dirname(BASE_DIR), "doc", "test_results.json")
LOCAL_REPORT = os.path.join(os.path.dirname(BASE_DIR), "doc", "test_report.md")

def connect_ssh():
    """建立SSH连接"""
    print(f"正在连接到 {HOST}...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    try:
        ssh.connect(HOST, username=USERNAME, password=PASSWORD, timeout=15)
        print(f"✓ SSH连接成功: {HOST}")
        return ssh
    except Exception as e:
        print(f"✗ SSH连接失败: {e}")
        return None

def upload_script(ssh, local_path, remote_path):
    """上传测试脚本"""
    print(f"\n正在上传测试脚本...")
    print(f"  本地: {local_path}")
    print(f"  远程: {remote_path}")
    
    try:
        sftp = ssh.open_sftp()
        sftp.put(local_path, remote_path)
        sftp.close()
        print(f"✓ 上传成功")
        return True
    except Exception as e:
        print(f"✗ 上传失败: {e}")
        return False

def execute_test(ssh, remote_script, timeout=300):
    """执行测试脚本"""
    print(f"\n正在执行测试脚本...")
    print("=" * 60)
    
    try:
        # 执行测试
        cmd = f"cd {REMOTE_PATH} && python3 {remote_script}"
        stdin, stdout, stderr = ssh.exec_command(cmd, timeout=timeout)
        
        # 实时输出
        output_lines = []
        while not stdout.channel.exit_status_ready():
            if stdout.channel.recv_ready():
                line = stdout.channel.recv(1024).decode('utf-8', errors='replace')
                print(line, end='')
                output_lines.append(line)
            time.sleep(0.1)
        
        # 获取剩余输出
        remaining = stdout.read().decode('utf-8', errors='replace')
        if remaining:
            print(remaining, end='')
            output_lines.append(remaining)
        
        # 获取错误输出
        errors = stderr.read().decode('utf-8', errors='replace')
        if errors:
            print(f"\n错误输出:\n{errors}")
        
        exit_status = stdout.channel.exit_status
        print("\n" + "=" * 60)
        print(f"测试执行完成，退出码: {exit_status}")
        
        return exit_status == 0
        
    except Exception as e:
        print(f"✗ 执行失败: {e}")
        return False

def download_results(ssh, remote_json, remote_md, local_json, local_md):
    """下载测试结果"""
    print(f"\n正在下载测试结果...")
    
    try:
        sftp = ssh.open_sftp()
        
        # 下载JSON结果
        try:
            sftp.get(remote_json, local_json)
            print(f"✓ JSON结果已下载: {local_json}")
        except Exception as e:
            print(f"✗ JSON结果下载失败: {e}")
        
        # 下载Markdown报告
        try:
            sftp.get(remote_md, local_md)
            print(f"✓ Markdown报告已下载: {local_md}")
        except Exception as e:
            print(f"✗ Markdown报告下载失败: {e}")
        
        sftp.close()
        return True
        
    except Exception as e:
        print(f"✗ 下载失败: {e}")
        return False

def print_summary(json_path):
    """打印测试摘要"""
    try:
        with open(json_path, 'r', encoding='utf-8') as f:
            results = json.load(f)
        
        total = len(results)
        passed = sum(1 for r in results if r['passed'])
        failed = total - passed
        pass_rate = (passed / total * 100) if total > 0 else 0
        
        print("\n" + "=" * 60)
        print("测试摘要")
        print("=" * 60)
        print(f"总测试数: {total}")
        print(f"通过: {passed}")
        print(f"失败: {failed}")
        print(f"通过率: {pass_rate:.1f}%")
        
        # 列出失败的测试
        failed_cases = [r for r in results if not r['passed']]
        if failed_cases:
            print(f"\n失败用例:")
            for case in failed_cases:
                print(f"  - [{case['test_id']}] {case['test_name']}: {case['error']}")
        
        print("=" * 60)
        
    except Exception as e:
        print(f"无法读取测试结果: {e}")

def main():
    print("=" * 60)
    print("任务2：协议测试执行")
    print("=" * 60)
    print(f"测试时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"目标主机: {HOST}")
    print(f"目标用户: {USERNAME}")
    print(f"远程路径: {REMOTE_PATH}")
    print()
    
    # 1. 连接SSH
    ssh = connect_ssh()
    if not ssh:
        return 1
    
    try:
        # 2. 上传测试脚本
        remote_script = REMOTE_PATH + "protocol_test.py"
        if not upload_script(ssh, LOCAL_SCRIPT, remote_script):
            return 1
        
        # 3. 执行测试
        if not execute_test(ssh, "protocol_test.py"):
            print("警告: 测试执行可能有问题")
        
        # 4. 下载结果
        remote_json = REMOTE_PATH + "test_results.json"
        remote_md = REMOTE_PATH + "test_report.md"
        download_results(ssh, remote_json, remote_md, LOCAL_RESULTS, LOCAL_REPORT)
        
        # 5. 打印摘要
        print_summary(LOCAL_RESULTS)
        
        print("\n测试完成！")
        print(f"结果文件:")
        print(f"  - JSON: {LOCAL_RESULTS}")
        print(f"  - Markdown: {LOCAL_REPORT}")
        
        return 0
        
    except Exception as e:
        print(f"执行过程中发生错误: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        ssh.close()
        print("\nSSH连接已关闭")

if __name__ == "__main__":
    sys.exit(main())
