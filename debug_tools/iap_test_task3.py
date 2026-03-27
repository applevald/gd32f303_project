#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IAP升级功能测试脚本 - 任务3
用于测试MCU的固件升级功能，重复测试5次

使用方法:
    python iap_test_task3.py --port /dev/ttyS4 --firmware gd32f303.bin
"""

import serial
import time
import argparse
import struct
import sys
import os
import json
from datetime import datetime

# 协议常量
FRAME_HEAD = 0xAE
FRAME_TAIL = 0xFE
TIMEOUT = 5.0
IAP_TIMEOUT = 10.0

# 命令码
CMD_IAP_REQUEST = 0xAA
CMD_IAP_PACKET = 0xAB
CMD_VERSION_QUERY = 0xAC
CMD_HEARTBEAT = 0xA0

# IAP参数
IAP_PACKET_SIZE = 256
BOOT_DELAY = 3.0      # Bootloader延迟时间
APP_DELAY = 5.0       # APP启动延迟时间
RETRY_DELAY = 2.0     # 重试延迟


class IAPTester:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        
    def connect(self):
        """连接串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=TIMEOUT
            )
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            return True, "连接成功"
        except Exception as e:
            return False, f"连接失败: {e}"
    
    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def calculate_checksum(self, data):
        """计算校验和"""
        return sum(data) & 0xFF
    
    def calculate_crc16(self, data):
        """计算CRC16 (Modbus)"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc
    
    def build_frame(self, cmd, data=None):
        """构建协议帧"""
        if data is None:
            data = []
        
        frame_length = 1 + 2 + 1 + len(data) + 1 + 1
        
        frame_data = bytes([FRAME_HEAD, (frame_length >> 8) & 0xFF, frame_length & 0xFF, cmd]) + bytes(data)
        checksum = self.calculate_checksum(frame_data)
        full_frame = frame_data + bytes([checksum, FRAME_TAIL])
        
        return full_frame
    
    def parse_frame(self, response):
        """解析接收到的帧"""
        if len(response) < 6:
            return None, "响应帧太短"
        
        if response[0] != FRAME_HEAD or response[-1] != FRAME_TAIL:
            return None, "帧头或帧尾错误"
        
        frame_length = (response[1] << 8) | response[2]
        if frame_length != len(response):
            return None, f"长度不匹配: 期望{frame_length}, 实际{len(response)}"
        
        cmd = response[3]
        data = response[4:-2]
        
        received_checksum = response[-2]
        calculated_checksum = self.calculate_checksum(response[:-2])
        if received_checksum != calculated_checksum:
            return None, f"校验和错误: 期望{calculated_checksum:02X}, 实际{received_checksum:02X}"
        
        return {'cmd': cmd, 'data': data}, None
    
    def send_and_receive(self, cmd, data=None, expected_cmd=None, timeout=TIMEOUT):
        """发送命令并接收响应"""
        frame = self.build_frame(cmd, data)
        
        try:
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            
            start_time = time.time()
            response = b''
            
            while time.time() - start_time < timeout:
                if self.ser.in_waiting > 0:
                    response += self.ser.read(self.ser.in_waiting)
                    
                    if len(response) >= 6 and response[0] == FRAME_HEAD:
                        frame_len = (response[1] << 8) | response[2]
                        if len(response) >= frame_len:
                            break
                
                time.sleep(0.01)
            
            if not response:
                return None, "超时: 无响应"
            
            parsed_frame, error = self.parse_frame(response)
            if error:
                return None, error
            
            if expected_cmd is not None and parsed_frame['cmd'] != expected_cmd:
                return None, f"命令码不匹配: 期望{expected_cmd:02X}, 实际{parsed_frame['cmd']:02X}"
            
            return parsed_frame, None
            
        except Exception as e:
            return None, f"通信异常: {str(e)}"
    
    def query_version(self):
        """查询固件版本"""
        response, error = self.send_and_receive(CMD_VERSION_QUERY, None, CMD_VERSION_QUERY)
        
        if error:
            return None, error
        
        if len(response['data']) >= 2:
            version = (response['data'][0] << 8) | response['data'][1]
            return version, None
        
        return None, "版本数据格式错误"
    
    def send_heartbeat(self):
        """发送心跳包"""
        magic = 0x12
        response, error = self.send_and_receive(CMD_HEARTBEAT, [magic], CMD_HEARTBEAT)
        
        if error:
            return False, error
        
        if len(response['data']) >= 1 and response['data'][0] == magic:
            return True, None
        
        return False, "心跳响应幻数不匹配"
    
    def wait_for_ready(self, timeout=10.0):
        """等待MCU准备就绪"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            success, _ = self.send_heartbeat()
            if success:
                return True
            time.sleep(0.5)
        return False
    
    def read_firmware(self, firmware_path):
        """读取固件文件"""
        try:
            with open(firmware_path, 'rb') as f:
                firmware = f.read()
            return firmware, None
        except Exception as e:
            return None, f"读取固件失败: {e}"
    
    def send_iap_request(self, firmware_size, total_packets, firmware_crc):
        """发送IAP请求"""
        data = bytearray()
        data.extend(struct.pack('<I', firmware_size))      # 固件大小
        data.extend(struct.pack('<H', IAP_PACKET_SIZE))    # 单包字节数
        data.extend(struct.pack('<H', total_packets))      # 包数
        data.extend(struct.pack('<H', firmware_crc))       # CRC16
        
        # MCU需要擦除Flash，可能需要较长时间（约30秒）
        response, error = self.send_and_receive(CMD_IAP_REQUEST, data, CMD_IAP_REQUEST, timeout=60.0)
        
        if error:
            return None, error
        
        result_code = response['data'][0] if len(response['data']) > 0 else 0xFF
        
        return result_code, None
    
    def send_iap_packet(self, seq, packet_data):
        """发送IAP数据包"""
        data = bytearray()
        data.extend(struct.pack('<H', seq))     # 包序列号
        data.extend(packet_data)                 # 固件数据
        
        response, error = self.send_and_receive(CMD_IAP_PACKET, data, timeout=IAP_TIMEOUT)
        
        if error:
            return None, error
        
        # 检查是否是升级完成响应 (返回0xAA命令码)
        if response['cmd'] == CMD_IAP_REQUEST:
            result_code = response['data'][0] if len(response['data']) > 0 else 0xFF
            if result_code == 1:  # 升级完成
                return 0xFFFF, None  # 特殊值表示升级完成
            else:
                return None, f"升级失败，结果码: {result_code}"
        
        if len(response['data']) >= 2:
            next_seq = struct.unpack('<H', response['data'][:2])[0]
            return next_seq, None
        
        return None, "响应数据格式错误"
    
    def do_upgrade(self, firmware_path, progress_callback=None):
        """执行固件升级"""
        # 读取固件
        firmware, error = self.read_firmware(firmware_path)
        if error:
            return False, error
        
        firmware_size = len(firmware)
        total_packets = (firmware_size + IAP_PACKET_SIZE - 1) // IAP_PACKET_SIZE
        firmware_crc = self.calculate_crc16(firmware)
        
        # 填充固件到整数倍包大小
        padded_size = total_packets * IAP_PACKET_SIZE
        padded_firmware = firmware + b'\x00' * (padded_size - firmware_size)
        
        # 发送IAP请求
        result_code, error = self.send_iap_request(firmware_size, total_packets, firmware_crc)
        
        if error:
            return False, f"IAP请求失败: {error}"
        
        if result_code != 0:
            return False, f"MCU拒绝升级请求，结果码: {result_code}"
        
        # 发送固件数据包
        for seq in range(total_packets):
            start = seq * IAP_PACKET_SIZE
            end = start + IAP_PACKET_SIZE
            packet_data = padded_firmware[start:end]
            
            next_seq, error = self.send_iap_packet(seq, packet_data)
            
            if error:
                return False, f"发送包 {seq} 失败: {error}"
            
            if progress_callback:
                progress_callback(seq + 1, total_packets)
            
            # 检查是否升级完成
            if next_seq == 0xFFFF:
                return True, None
        
        return True, None


def run_single_test(tester, firmware_path, test_num):
    """运行单次IAP升级测试"""
    result = {
        'test_num': test_num,
        'timestamp': datetime.now().isoformat(),
        'steps': [],
        'success': False,
        'error': None
    }
    
    def add_step(step_name, success, message=''):
        result['steps'].append({
            'step': step_name,
            'success': success,
            'message': message,
            'timestamp': datetime.now().isoformat()
        })
        status = 'OK' if success else 'FAIL'
        print(f"  [{status}] {step_name}: {message}")
    
    # Step 1: 连接串口
    success, msg = tester.connect()
    add_step("连接串口", success, msg)
    if not success:
        result['error'] = msg
        return result
    
    try:
        # Step 2: 查询当前版本
        version, error = tester.query_version()
        if error:
            add_step("查询当前版本", False, error)
        else:
            add_step("查询当前版本", True, f"V{version}")
        
        # Step 3: 发送IAP请求并升级
        print(f"  开始固件升级...")
        progress_printed = [0]  # 使用列表以便在闭包中修改
        
        def progress_callback(current, total):
            progress = current * 100 // total
            if progress >= progress_printed[0] + 10 or current == total:
                print(f"    进度: {current}/{total} ({progress}%)")
                progress_printed[0] = progress // 10 * 10
        
        success, error = tester.do_upgrade(firmware_path, progress_callback)
        add_step("固件传输", success, error if error else "传输完成")
        
        if not success:
            result['error'] = error
            return result
        
        # Step 4: 断开连接，等待MCU重启
        tester.disconnect()
        add_step("等待重启", True, f"等待{BOOT_DELAY + APP_DELAY}秒...")
        time.sleep(BOOT_DELAY + APP_DELAY)
        
        # Step 5: 重新连接
        success, msg = tester.connect()
        add_step("重新连接", success, msg)
        if not success:
            result['error'] = msg
            return result
        
        # Step 6: 等待MCU就绪
        ready = tester.wait_for_ready(timeout=10.0)
        add_step("等待MCU就绪", ready, "就绪" if ready else "超时")
        
        if not ready:
            result['error'] = "MCU未就绪"
            return result
        
        # Step 7: 验证版本（升级后版本应该相同，因为是同一个固件）
        version, error = tester.query_version()
        if error:
            add_step("验证升级后版本", False, error)
            result['error'] = error
        else:
            add_step("验证升级后版本", True, f"V{version}")
            result['success'] = True
        
    finally:
        tester.disconnect()
    
    return result


def main():
    parser = argparse.ArgumentParser(description='IAP升级功能测试 - 任务3')
    parser.add_argument('--port', default='/dev/ttyS4', help='串口设备')
    parser.add_argument('--baudrate', type=int, default=115200, help='波特率')
    parser.add_argument('--firmware', required=True, help='固件文件路径')
    parser.add_argument('--count', type=int, default=5, help='测试次数')
    parser.add_argument('--output', default='iap_test_report.json', help='输出报告文件名')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("IAP升级功能测试 - 任务3")
    print("=" * 60)
    print(f"串口: {args.port}")
    print(f"固件: {args.firmware}")
    print(f"测试次数: {args.count}")
    print("=" * 60)
    
    # 检查固件文件
    if not os.path.exists(args.firmware):
        print(f"[FAIL] 固件文件不存在: {args.firmware}")
        return 1
    
    firmware_size = os.path.getsize(args.firmware)
    print(f"固件大小: {firmware_size} 字节")
    
    # 开始测试
    tester = IAPTester(args.port, args.baudrate)
    results = []
    
    for i in range(1, args.count + 1):
        print(f"\n{'='*60}")
        print(f"第 {i}/{args.count} 次测试")
        print("=" * 60)
        
        result = run_single_test(tester, args.firmware, i)
        results.append(result)
        
        status = "成功" if result['success'] else f"失败: {result['error']}"
        print(f"\n第 {i} 次测试结果: {status}")
        
        # 如果失败，等待一段时间再重试
        if not result['success'] and i < args.count:
            print(f"等待 {RETRY_DELAY} 秒后继续...")
            time.sleep(RETRY_DELAY)
    
    # 生成测试报告
    success_count = sum(1 for r in results if r['success'])
    fail_count = args.count - success_count
    
    print("\n" + "=" * 60)
    print("测试报告")
    print("=" * 60)
    print(f"总测试次数: {args.count}")
    print(f"成功次数: {success_count}")
    print(f"失败次数: {fail_count}")
    print(f"成功率: {success_count * 100 / args.count:.1f}%")
    
    print("\n详细结果:")
    for r in results:
        status = "成功" if r['success'] else f"失败"
        print(f"  测试 {r['test_num']}: {status}")
        if not r['success']:
            print(f"    错误: {r['error']}")
    
    # 保存JSON报告
    report = {
        'test_info': {
            'firmware': args.firmware,
            'firmware_size': firmware_size,
            'port': args.port,
            'baudrate': args.baudrate,
            'test_count': args.count,
            'test_time': datetime.now().isoformat()
        },
        'summary': {
            'total': args.count,
            'success': success_count,
            'fail': fail_count,
            'success_rate': success_count * 100 / args.count
        },
        'results': results
    }
    
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    print(f"\n报告已保存: {args.output}")
    
    # 生成Markdown报告
    md_report = f"""# IAP升级功能测试报告

## 测试信息

- **固件文件**: `{args.firmware}`
- **固件大小**: {firmware_size} 字节
- **串口**: {args.port}
- **波特率**: {args.baudrate}
- **测试时间**: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

## 测试结果摘要

| 项目 | 数值 |
|------|------|
| 总测试次数 | {args.count} |
| 成功次数 | {success_count} |
| 失败次数 | {fail_count} |
| 成功率 | {success_count * 100 / args.count:.1f}% |

## 详细测试结果

"""
    
    for r in results:
        status = "成功" if r['success'] else f"失败: {r['error']}"
        md_report += f"### 测试 {r['test_num']}: {status}\n\n"
        md_report += "| 步骤 | 结果 | 说明 |\n|------|------|------|\n"
        for step in r['steps']:
            step_status = "OK" if step['success'] else "FAIL"
            md_report += f"| {step['step']} | {step_status} | {step['message']} |\n"
        md_report += "\n"
    
    md_filename = args.output.replace('.json', '.md')
    with open(md_filename, 'w', encoding='utf-8') as f:
        f.write(md_report)
    print(f"Markdown报告已保存: {md_filename}")
    
    return 0 if success_count == args.count else 1


if __name__ == '__main__':
    sys.exit(main())
