#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
远程测试执行脚本
使用SSH连接到目标主机并执行协议测试
"""

import paramiko
import sys
import os
from datetime import datetime

# 配置信息
TARGET_HOST = "10.0.5.210"
USERNAME = "root"
PASSWORD = "123"
REMOTE_PATH = "/root/"
SERIAL_PORT = "/dev/ttyS4"

# 本地文件路径
LOCAL_SCRIPT = r"D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\debug_tools\protocol_test.py"
LOCAL_RESULTS = r"D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\test_results.json"
LOCAL_REPORT = r"D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\test_report.md"


def create_ssh_client():
    """创建SSH客户端连接"""
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    try:
        print(f"正在连接到 {TARGET_HOST}...")
        client.connect(TARGET_HOST, username=USERNAME, password=PASSWORD, timeout=10)
        print("✓ SSH连接成功")
        return client
    except Exception as e:
        print(f"✗ SSH连接失败: {e}")
        return None


def upload_file(client, local_path, remote_path):
    """上传文件到远程主机"""
    try:
        print(f"正在上传文件: {os.path.basename(local_path)}")
        sftp = client.open_sftp()
        sftp.put(local_path, remote_path)
        sftp.close()
        print(f"✓ 文件上传成功: {remote_path}")
        return True
    except Exception as e:
        print(f"✗ 文件上传失败: {e}")
        return False


def execute_command(client, command):
    """在远程主机上执行命令"""
    try:
        print(f"执行命令: {command}")
        stdin, stdout, stderr = client.exec_command(command, get_pty=True)
        
        # 读取输出
        output = stdout.read().decode('utf-8')
        error = stderr.read().decode('utf-8')
        
        # 等待命令完成
        exit_status = stdout.channel.recv_exit_status()
        
        if output:
            print(output)
        
        if error:
            print(f"错误输出: {error}")
        
        return exit_status, output, error
    except Exception as e:
        print(f"✗ 命令执行失败: {e}")
        return -1, "", str(e)


def download_file(client, remote_path, local_path):
    """从远程主机下载文件"""
    try:
        print(f"正在下载文件: {os.path.basename(local_path)}")
        sftp = client.open_sftp()
        
        # 检查远程文件是否存在
        try:
            sftp.stat(remote_path)
        except IOError:
            print(f"✗ 远程文件不存在: {remote_path}")
            return False
        
        sftp.get(remote_path, local_path)
        sftp.close()
        print(f"✓ 文件下载成功: {local_path}")
        return True
    except Exception as e:
        print(f"✗ 文件下载失败: {e}")
        return False


def main():
    print("=" * 60)
    print("远程测试执行脚本")
    print("=" * 60)
    print(f"目标主机: {TARGET_HOST}")
    print(f"用户名: {USERNAME}")
    print(f"串口设备: {SERIAL_PORT}")
    print(f"开始时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("")
    
    # 检查本地文件
    if not os.path.exists(LOCAL_SCRIPT):
        print(f"✗ 本地文件不存在: {LOCAL_SCRIPT}")
        return 1
    
    # 创建SSH连接
    client = create_ssh_client()
    if not client:
        print("\n请手动执行以下步骤:")
        print(f"1. 复制 {LOCAL_SCRIPT} 到 {TARGET_HOST}:{REMOTE_PATH}")
        print(f"2. SSH登录: ssh {USERNAME}@{TARGET_HOST}")
        print(f"3. 执行测试: cd {REMOTE_PATH}; python3 protocol_test.py")
        return 1
    
    try:
        # 步骤1: 上传测试脚本
        print("\n[步骤1] 上传测试脚本")
        remote_script = os.path.join(REMOTE_PATH, "protocol_test.py")
        if not upload_file(client, LOCAL_SCRIPT, remote_script):
            return 1
        
        # 步骤2: 检查Python环境
        print("\n[步骤2] 检查Python环境")
        exit_code, _, _ = execute_command(client, "python3 --version")
        if exit_code != 0:
            print("✗ Python3未安装或不可用")
            return 1
        
        # 检查pyserial库
        exit_code, _, _ = execute_command(client, "python3 -c 'import serial'")
        if exit_code != 0:
            print("正在安装pyserial库...")
            # 尝试使用apt安装
            exit_code, _, _ = execute_command(client, "apt-get update && apt-get install -y python3-serial")
            if exit_code != 0:
                print("✗ pyserial库安装失败")
                return 1
        
        # 检查串口设备
        print("\n[步骤3] 检查串口设备")
        exit_code, output, _ = execute_command(client, f"ls -la {SERIAL_PORT}")
        if exit_code != 0:
            print(f"✗ 串口设备不存在: {SERIAL_PORT}")
            print(f"提示: 请检查串口设备路径是否正确")
            return 1
        else:
            print(f"✓ 串口设备存在: {SERIAL_PORT}")
        
        # 步骤4: 执行测试
        print("\n[步骤4] 执行协议测试")
        test_command = f"cd {REMOTE_PATH} && python3 protocol_test.py"
        exit_code, output, error = execute_command(client, test_command)
        
        if exit_code == 0:
            print("✓ 测试执行完成")
        else:
            print(f"⚠ 测试执行完成，退出码: {exit_code}")
        
        # 步骤5: 下载测试结果
        print("\n[步骤5] 下载测试结果")
        
        # 创建本地目录
        os.makedirs(os.path.dirname(LOCAL_RESULTS), exist_ok=True)
        
        # 下载JSON结果
        remote_json = os.path.join(REMOTE_PATH, "test_results.json")
        if download_file(client, remote_json, LOCAL_RESULTS):
            print(f"✓ JSON结果已保存: {LOCAL_RESULTS}")
        else:
            print("⚠ 无法下载JSON结果")
        
        # 下载Markdown报告
        remote_md = os.path.join(REMOTE_PATH, "test_report.md")
        if download_file(client, remote_md, LOCAL_REPORT):
            print(f"✓ Markdown报告已保存: {LOCAL_REPORT}")
        else:
            print("⚠ 无法下载Markdown报告")
        
        print("\n" + "=" * 60)
        print("远程测试执行完成")
        print("=" * 60)
        print(f"结束时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("")
        
        # 显示结果文件位置
        if os.path.exists(LOCAL_RESULTS):
            print(f"测试结果文件:")
            print(f"  - JSON: {LOCAL_RESULTS}")
        if os.path.exists(LOCAL_REPORT):
            print(f"  - Markdown: {LOCAL_REPORT}")
        
        return 0
        
    except Exception as e:
        print(f"\n✗ 执行过程中发生异常: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        client.close()
        print("\n✓ SSH连接已关闭")


if __name__ == '__main__':
    sys.exit(main())