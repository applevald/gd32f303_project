#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IAP升级测试脚本
用于测试MCU的固件升级功能

使用方法:
    python iap_test.py --port /dev/ttyS4 --firmware app.bin
"""

import serial
import time
import argparse
import struct
import sys
import os

# 协议常量
FRAME_HEAD = 0xAE
FRAME_TAIL = 0xFE
TIMEOUT = 5.0

# 命令码
CMD_IAP_REQUEST = 0xAA
CMD_IAP_PACKET = 0xAB

# IAP参数
IAP_PACKET_SIZE = 256


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
            print(f"[OK] 已连接串口: {self.port}")
            return True
        except Exception as e:
            print(f"[FAIL] 串口连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[OK] 串口已关闭")
    
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
            return None, f"长度不匹配"
        
        cmd = response[3]
        data = response[4:-2]
        
        received_checksum = response[-2]
        calculated_checksum = self.calculate_checksum(response[:-2])
        if received_checksum != calculated_checksum:
            return None, f"校验和错误"
        
        return {'cmd': cmd, 'data': data}, None
    
    def send_and_receive(self, cmd, data=None, expected_cmd=None, timeout=TIMEOUT):
        """发送命令并接收响应"""
        frame = self.build_frame(cmd, data)
        
        print(f"  发送: {' '.join(f'{b:02X}' for b in frame[:20])}{'...' if len(frame) > 20 else ''}")
        
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
            
            print(f"  接收: {' '.join(f'{b:02X}' for b in response[:20])}{'...' if len(response) > 20 else ''}")
            
            parsed_frame, error = self.parse_frame(response)
            if error:
                return None, error
            
            if expected_cmd is not None and parsed_frame['cmd'] != expected_cmd:
                return None, f"命令码不匹配: 期望{expected_cmd:02X}, 实际{parsed_frame['cmd']:02X}"
            
            return parsed_frame, None
            
        except Exception as e:
            return None, f"通信异常: {str(e)}"
    
    def read_firmware(self, firmware_path):
        """读取固件文件"""
        try:
            with open(firmware_path, 'rb') as f:
                firmware = f.read()
            print(f"[OK] 读取固件: {firmware_path}, 大小: {len(firmware)} 字节")
            return firmware
        except Exception as e:
            print(f"[FAIL] 读取固件失败: {e}")
            return None
    
    def send_iap_request(self, firmware_size, total_packets, firmware_crc):
        """发送IAP请求"""
        # 构建请求参数（小端字节序）
        data = bytearray()
        data.extend(struct.pack('<I', firmware_size))      # 固件大小
        data.extend(struct.pack('<H', IAP_PACKET_SIZE))    # 单包字节数
        data.extend(struct.pack('<H', total_packets))      # 包数
        data.extend(struct.pack('<H', firmware_crc))       # CRC16
        
        print(f"\n[IAP] 发送升级请求:")
        print(f"  固件大小: {firmware_size} 字节")
        print(f"  单包大小: {IAP_PACKET_SIZE} 字节")
        print(f"  总包数: {total_packets}")
        print(f"  CRC16: 0x{firmware_crc:04X}")
        
        response, error = self.send_and_receive(CMD_IAP_REQUEST, data, CMD_IAP_REQUEST)
        
        if error:
            return None, error
        
        result_code = response['data'][0] if len(response['data']) > 0 else 0xFF
        
        return result_code, None
    
    def send_iap_packet(self, seq, packet_data):
        """发送IAP数据包"""
        # 构建数据包
        data = bytearray()
        data.extend(struct.pack('<H', seq))     # 包序列号
        data.extend(packet_data)                 # 固件数据
        
        response, error = self.send_and_receive(CMD_IAP_PACKET, data, CMD_IAP_PACKET, timeout=10)
        
        if error:
            # 检查是否是升级完成响应
            response, error = self.send_and_receive(CMD_IAP_PACKET, data, CMD_IAP_REQUEST, timeout=10)
            if error:
                return None, error
            # 升级完成
            return 0xFFFF, None  # 特殊值表示升级完成
        
        if len(response['data']) >= 2:
            next_seq = struct.unpack('<H', response['data'][:2])[0]
            return next_seq, None
        
        return None, "响应数据格式错误"
    
    def do_upgrade(self, firmware_path):
        """执行固件升级"""
        # 读取固件
        firmware = self.read_firmware(firmware_path)
        if firmware is None:
            return False
        
        firmware_size = len(firmware)
        total_packets = (firmware_size + IAP_PACKET_SIZE - 1) // IAP_PACKET_SIZE
        firmware_crc = self.calculate_crc16(firmware)
        
        # 填充固件到整数倍包大小
        padded_size = total_packets * IAP_PACKET_SIZE
        padded_firmware = firmware + b'\x00' * (padded_size - firmware_size)
        
        # 发送IAP请求
        result_code, error = self.send_iap_request(firmware_size, total_packets, firmware_crc)
        
        if error:
            print(f"[FAIL] IAP请求失败: {error}")
            return False
        
        if result_code != 0:
            print(f"[FAIL] MCU拒绝升级请求, 结果码: {result_code}")
            return False
        
        print(f"[OK] MCU接受升级请求")
        
        # 发送固件数据包
        print(f"\n[IAP] 开始发送固件数据...")
        
        for seq in range(total_packets):
            # 获取当前包数据
            start = seq * IAP_PACKET_SIZE
            end = start + IAP_PACKET_SIZE
            packet_data = padded_firmware[start:end]
            
            # 发送数据包
            next_seq, error = self.send_iap_packet(seq, packet_data)
            
            if error:
                print(f"[FAIL] 发送包 {seq} 失败: {error}")
                return False
            
            # 显示进度
            progress = (seq + 1) * 100 // total_packets
            if (seq % 10) == 0 or seq == total_packets - 1:
                print(f"  进度: {seq + 1}/{total_packets} ({progress}%)")
            
            # 检查是否升级完成
            if next_seq == 0xFFFF:
                print(f"\n[OK] 固件发送完成!")
                return True
        
        # 等待最终响应
        print(f"\n[OK] 固件发送完成，等待MCU验证...")
        time.sleep(1)
        
        return True


def main():
    parser = argparse.ArgumentParser(description='IAP升级测试工具')
    parser.add_argument('--port', default='/dev/ttyS4', help='串口设备')
    parser.add_argument('--baudrate', type=int, default=115200, help='波特率')
    parser.add_argument('--firmware', required=True, help='固件文件路径')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("IAP升级测试工具")
    print("=" * 60)
    
    tester = IAPTester(args.port, args.baudrate)
    
    if not tester.connect():
        return 1
    
    try:
        success = tester.do_upgrade(args.firmware)
        
        if success:
            print("\n" + "=" * 60)
            print("[SUCCESS] 升级完成!")
            print("=" * 60)
            print("系统将在几秒后重启，Bootloader将执行升级...")
            return 0
        else:
            print("\n" + "=" * 60)
            print("[FAILED] 升级失败!")
            print("=" * 60)
            return 1
    
    except KeyboardInterrupt:
        print("\n\n[INTERRUPT] 用户中断")
        return 1
    
    finally:
        tester.disconnect()


if __name__ == '__main__':
    sys.exit(main())
