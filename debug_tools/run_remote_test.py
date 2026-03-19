#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
在目标主机上执行测试脚本并获取结果
"""

import paramiko
import sys
import time

def execute_remote_test(host, username, password, remote_script_path, timeout=300):
    """在远程主机上执行测试脚本"""
    try:
        # 创建SSH客户端
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        # 连接到远程主机
        print(f"连接到远程主机 {host}...")
        ssh.connect(host, username=username, password=password, timeout=10)

        # 执行测试脚本
        print(f"执行测试脚本: {remote_script_path}")
        stdin, stdout, stderr = ssh.exec_command(f'cd /tmp && python3 {remote_script_path}', timeout=timeout)

        # 读取输出
        print("\n" + "="*60)
        print("测试输出:")
        print("="*60)
        output = []
        for line in stdout:
            print(line.strip())
            output.append(line.strip())

        # 读取错误
        errors = []
        for line in stderr:
            errors.append(line.strip())

        # 等待命令完成
        exit_status = stdout.channel.recv_exit_status()

        # 下载测试结果文件
        print("\n" + "="*60)
        print("下载测试结果...")
        print("="*60)

        sftp = ssh.open_sftp()

        # 下载JSON结果
        try:
            sftp.get('/tmp/test_results.json', 'test_results.json')
            print("✓ 已下载 test_results.json")
        except:
            print("✗ 无法下载 test_results.json")

        # 下载Markdown报告
        try:
            sftp.get('/tmp/test_report.md', 'test_report.md')
            print("✓ 已下载 test_report.md")
        except:
            print("✗ 无法下载 test_report.md")

        sftp.close()
        ssh.close()

        if exit_status != 0:
            print(f"\n✗ 测试执行失败，退出码: {exit_status}")
            if errors:
                print("错误信息:")
                for err in errors:
                    print(f"  {err}")
            return False, None, None

        print(f"\n✓ 测试执行成功，退出码: {exit_status}")
        return True, output, errors

    except Exception as e:
        print(f"✗ 执行失败: {e}")
        import traceback
        traceback.print_exc()
        return False, None, None

if __name__ == '__main__':
    host = '10.0.5.210'
    username = 'root'
    password = '123'
    remote_script_path = 'protocol_test.py'

    success, output, errors = execute_remote_test(host, username, password, remote_script_path)

    if not success:
        sys.exit(1)

    print("\n" + "="*60)
    print("测试完成！")
    print("="*60)
    print("结果文件:")
    print("  - test_results.json")
    print("  - test_report.md")

    sys.exit(0)