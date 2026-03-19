#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将测试脚本上传到目标主机
"""

import paramiko
import sys

def upload_file(host, username, password, local_path, remote_path):
    """上传文件到远程主机"""
    try:
        # 创建SSH客户端
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        # 连接到远程主机
        ssh.connect(host, username=username, password=password, timeout=10)

        # 使用SFTP上传文件
        sftp = ssh.open_sftp()
        sftp.put(local_path, remote_path)
        sftp.close()

        # 关闭连接
        ssh.close()

        print(f"✓ 文件已成功上传到 {host}:{remote_path}")
        return True

    except Exception as e:
        print(f"✗ 上传失败: {e}")
        return False

if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("用法: python upload_test.py <host> <username> <password> <local_path> [remote_path]")
        sys.exit(1)

    host = sys.argv[1]
    username = sys.argv[2]
    password = sys.argv[3]
    local_path = sys.argv[4]
    remote_path = sys.argv[5] if len(sys.argv) > 5 else '/tmp/protocol_test.py'

    success = upload_file(host, username, password, local_path, remote_path)
    sys.exit(0 if success else 1)