#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IAP升级测试 - 远程执行脚本
将测试脚本和固件上传到目标主机并执行测试
"""

import paramiko
import sys
import os
import time

# 目标主机配置（备用测试主机）
HOST = '10.0.5.204'
USERNAME = 'root'
PASSWORD = '123'
SERIAL_PORT = '/dev/ttyS4'

# 文件路径配置
LOCAL_DIR = os.path.dirname(os.path.abspath(__file__))
FIRMWARE_FILE = os.path.join(os.path.dirname(LOCAL_DIR), 'doc', 'gd32f303.bin')
TEST_SCRIPT = os.path.join(LOCAL_DIR, 'iap_test_task3.py')

# 远程路径
REMOTE_DIR = '/tmp/iap_test'
REMOTE_FIRMWARE = f'{REMOTE_DIR}/gd32f303.bin'
REMOTE_SCRIPT = f'{REMOTE_DIR}/iap_test_task3.py'


def connect_ssh():
    """创建SSH连接"""
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(HOST, username=USERNAME, password=PASSWORD, timeout=10)
    return ssh


def upload_files(ssh):
    """上传测试文件"""
    sftp = ssh.open_sftp()
    
    # 创建远程目录
    try:
        sftp.mkdir(REMOTE_DIR)
        print(f"[OK] 创建远程目录: {REMOTE_DIR}")
    except:
        print(f"[OK] 远程目录已存在: {REMOTE_DIR}")
    
    # 上传固件文件
    if not os.path.exists(FIRMWARE_FILE):
        print(f"[FAIL] 固件文件不存在: {FIRMWARE_FILE}")
        return False
    
    print(f"[OK] 上传固件: {FIRMWARE_FILE}")
    sftp.put(FIRMWARE_FILE, REMOTE_FIRMWARE)
    firmware_size = os.path.getsize(FIRMWARE_FILE)
    print(f"     大小: {firmware_size} 字节")
    
    # 上传测试脚本
    print(f"[OK] 上传测试脚本: {TEST_SCRIPT}")
    sftp.put(TEST_SCRIPT, REMOTE_SCRIPT)
    
    sftp.close()
    return True


def execute_test(ssh, test_count=5):
    """执行IAP测试"""
    cmd = f'cd {REMOTE_DIR} && python3 iap_test_task3.py --port {SERIAL_PORT} --firmware gd32f303.bin --count {test_count}'
    
    print(f"\n{'='*60}")
    print(f"执行测试命令: {cmd}")
    print('='*60)
    
    stdin, stdout, stderr = ssh.exec_command(cmd, timeout=600)
    
    # 实时输出
    while True:
        line = stdout.readline()
        if not line:
            break
        print(line.rstrip())
    
    # 读取错误
    errors = stderr.read().decode('utf-8')
    if errors:
        print(f"\n[错误输出]\n{errors}")
    
    exit_status = stdout.channel.recv_exit_status()
    return exit_status


def download_results(ssh):
    """下载测试结果"""
    sftp = ssh.open_sftp()
    
    local_json = os.path.join(LOCAL_DIR, 'iap_test_report.json')
    local_md = os.path.join(LOCAL_DIR, 'iap_test_report.md')
    
    remote_json = f'{REMOTE_DIR}/iap_test_report.json'
    remote_md = f'{REMOTE_DIR}/iap_test_report.md'
    
    try:
        sftp.get(remote_json, local_json)
        print(f"[OK] 下载报告: {local_json}")
    except Exception as e:
        print(f"[WARN] 无法下载JSON报告: {e}")
    
    try:
        sftp.get(remote_md, local_md)
        print(f"[OK] 下载报告: {local_md}")
    except Exception as e:
        print(f"[WARN] 无法下载Markdown报告: {e}")
    
    sftp.close()


def main():
    print("=" * 60)
    print("IAP升级功能测试 - 任务3")
    print("=" * 60)
    print(f"目标主机: {HOST}")
    print(f"串口设备: {SERIAL_PORT}")
    print(f"固件文件: {FIRMWARE_FILE}")
    print("=" * 60)
    
    # 检查本地文件
    if not os.path.exists(FIRMWARE_FILE):
        print(f"[FAIL] 固件文件不存在: {FIRMWARE_FILE}")
        return 1
    
    if not os.path.exists(TEST_SCRIPT):
        print(f"[FAIL] 测试脚本不存在: {TEST_SCRIPT}")
        return 1
    
    # 连接SSH
    print("\n[1] 连接目标主机...")
    try:
        ssh = connect_ssh()
        print(f"[OK] 已连接到 {HOST}")
    except Exception as e:
        print(f"[FAIL] SSH连接失败: {e}")
        return 1
    
    try:
        # 上传文件
        print("\n[2] 上传测试文件...")
        if not upload_files(ssh):
            return 1
        
        # 执行测试
        print("\n[3] 执行IAP升级测试...")
        exit_status = execute_test(ssh, test_count=5)
        
        # 下载结果
        print("\n[4] 下载测试报告...")
        download_results(ssh)
        
        print("\n" + "=" * 60)
        if exit_status == 0:
            print("[SUCCESS] 所有测试通过!")
        else:
            print(f"[WARNING] 部分测试失败，退出码: {exit_status}")
        print("=" * 60)
        
        return exit_status
        
    except Exception as e:
        print(f"[FAIL] 执行失败: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    finally:
        ssh.close()


if __name__ == '__main__':
    sys.exit(main())
