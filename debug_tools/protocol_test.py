#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
助手控制板通信协议测试脚本
支持45个测试用例的自动化执行
"""

import serial
import time
import json
import sys
from datetime import datetime

# 协议常量
FRAME_HEAD = 0xAE
FRAME_TAIL = 0xFE
FRAME_HEAD_STR = "AE"
FRAME_TAIL_STR = "FE"
TIMEOUT = 3.0  # 3秒超时

# 命令码
CMD_HEARTBEAT = 0xA0
CMD_SET_FAN = 0xA1
CMD_FAN_GETSPEED = 0xA2
CMD_FAN_STATUS = 0xA3
CMD_COLOR_LIGHT = 0xA4
CMD_LIGHT_BAR = 0xA5
CMD_ALL_STATUS = 0xA6
CMD_TEMP_SET = 0xA7
CMD_WINDOWS_CONTROL = 0xA8
CMD_RESPONSE_ERROR = 0xAE


class ProtocolTest:
    def __init__(self, port='/dev/ttyS4', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.test_results = []
        self.connect()

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
            print(f"✓ 成功连接串口: {self.port}")
            return True
        except Exception as e:
            print(f"✗ 串口连接失败: {e}")
            return False

    def disconnect(self):
        """断开串口连接"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("✓ 串口已关闭")

    def calculate_checksum(self, data):
        """计算校验和"""
        checksum = sum(data) & 0xFF
        return checksum

    def build_frame(self, cmd, data=None):
        """构建协议帧"""
        if data is None:
            data = []
        
        # 计算帧长度（从帧头到帧尾）
        frame_length = 1 + 2 + 1 + len(data) + 1 + 1  # 帧头 + 长度(2) + 命令 + 数据 + 校验 + 帧尾
        
        # 构建帧数据（不包含校验和帧尾）
        frame_data = bytes([FRAME_HEAD, (frame_length >> 8) & 0xFF, frame_length & 0xFF, cmd]) + bytes(data)
        
        # 计算校验和
        checksum = self.calculate_checksum(frame_data)
        
        # 完整帧
        full_frame = frame_data + bytes([checksum, FRAME_TAIL])
        
        return full_frame

    def parse_frame(self, response):
        """解析接收到的帧"""
        if len(response) < 6:
            return None, "响应帧太短"
        
        # 检查帧头和帧尾
        if response[0] != FRAME_HEAD or response[-1] != FRAME_TAIL:
            return None, "帧头或帧尾错误"
        
        # 解析长度
        frame_length = (response[1] << 8) | response[2]
        if frame_length != len(response):
            return None, f"长度不匹配: 期望{frame_length}, 实际{len(response)}"
        
        # 解析命令码
        cmd = response[3]
        
        # 提取数据区
        data = response[4:-2]
        
        # 验证校验和
        received_checksum = response[-2]
        calculated_checksum = self.calculate_checksum(response[:-2])
        if received_checksum != calculated_checksum:
            return None, f"校验和错误: 接收{received_checksum:02X}, 计算{calculated_checksum:02X}"
        
        return {'cmd': cmd, 'data': data}, None

    def send_and_receive(self, cmd, data=None, expected_cmd=None):
        """发送命令并接收响应"""
        # 构建并发送帧
        frame = self.build_frame(cmd, data)
        
        print(f"  发送: {' '.join(f'{b:02X}' for b in frame)}")
        
        try:
            # 清空接收缓冲区
            self.ser.reset_input_buffer()
            
            # 发送数据
            self.ser.write(frame)
            self.ser.flush()
            
            # 接收响应
            start_time = time.time()
            response = b''
            
            while time.time() - start_time < TIMEOUT:
                if self.ser.in_waiting > 0:
                    response += self.ser.read(self.ser.in_waiting)
                    
                    # 检查是否接收到完整帧
                    if len(response) >= 6 and response[0] == FRAME_HEAD:
                        # 尝试解析长度
                        frame_len = (response[1] << 8) | response[2]
                        if len(response) >= frame_len:
                            break
                
                time.sleep(0.01)
            
            if not response:
                return None, "超时: 3秒内无响应"
            
            print(f"  接收: {' '.join(f'{b:02X}' for b in response)}")
            
            # 解析响应
            parsed_frame, error = self.parse_frame(response)
            if error:
                return None, error
            
            # 验证命令码（如果有期望值）
            if expected_cmd is not None and parsed_frame['cmd'] != expected_cmd:
                # 检查是否是错误应答
                if parsed_frame['cmd'] == CMD_RESPONSE_ERROR:
                    error_code = parsed_frame['data'][1] if len(parsed_frame['data']) > 1 else 0
                    return None, f"收到错误应答: 原命令码={parsed_frame['data'][0]:02X}, 错误码={error_code:02X}"
                return None, f"命令码不匹配: 期望{expected_cmd:02X}, 实际{parsed_frame['cmd']:02X}"
            
            return parsed_frame, None
            
        except Exception as e:
            return None, f"通信异常: {str(e)}"

    def add_test_result(self, test_id, test_name, passed, expected, actual, error=None):
        """添加测试结果"""
        result = {
            'test_id': test_id,
            'test_name': test_name,
            'passed': passed,
            'expected': expected,
            'actual': actual,
            'error': error,
            'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        }
        self.test_results.append(result)
        
        # 打印结果
        status = "✓ 通过" if passed else "✗ 失败"
        print(f"  结果: {status}")
        if error:
            print(f"  错误: {error}")

    # ==================== 测试用例 ====================

    def test_heartbeat(self):
        """测试心跳帧"""
        print("\n" + "="*60)
        print("测试组: 心跳帧 (0xA0)")
        print("="*60)
        
        # TC_A0_001: 标准心跳帧
        print("\n[TC_A0_001] 标准心跳帧")
        response, error = self.send_and_receive(CMD_HEARTBEAT, [0x12], CMD_HEARTBEAT)
        if error:
            self.add_test_result('TC_A0_001', '标准心跳帧', False, '幻数=0x12', None, error)
        else:
            magic = response['data'][0] if len(response['data']) > 0 else None
            passed = (magic == 0x12)
            self.add_test_result('TC_A0_001', '标准心跳帧', passed, '幻数=0x12', f'幻数={magic:02X}', 
                              None if passed else "幻数不匹配")
        
        # TC_A0_002: 心跳帧幻数变化测试
        print("\n[TC_A0_002] 心跳帧幻数变化测试")
        response, error = self.send_and_receive(CMD_HEARTBEAT, [0x34], CMD_HEARTBEAT)
        if error:
            self.add_test_result('TC_A0_002', '心跳帧幻数变化测试', False, '幻数=0x34', None, error)
        else:
            magic = response['data'][0] if len(response['data']) > 0 else None
            passed = (magic == 0x34)
            self.add_test_result('TC_A0_002', '心跳帧幻数变化测试', passed, '幻数=0x34', f'幻数={magic:02X}', 
                              None if passed else "幻数不匹配")
        
        # TC_A0_003: 心跳帧边界值测试 - 幻数0x00
        print("\n[TC_A0_003] 心跳帧边界值测试 - 幻数0x00")
        response, error = self.send_and_receive(CMD_HEARTBEAT, [0x00], CMD_HEARTBEAT)
        if error:
            self.add_test_result('TC_A0_003', '心跳帧边界值测试 - 幻数0x00', False, '幻数=0x00', None, error)
        else:
            magic = response['data'][0] if len(response['data']) > 0 else None
            passed = (magic == 0x00)
            self.add_test_result('TC_A0_003', '心跳帧边界值测试 - 幻数0x00', passed, '幻数=0x00', f'幻数={magic:02X}', 
                              None if passed else "幻数不匹配")
        
        # TC_A0_004: 心跳帧边界值测试 - 幻数0xFF
        print("\n[TC_A0_004] 心跳帧边界值测试 - 幻数0xFF")
        response, error = self.send_and_receive(CMD_HEARTBEAT, [0xFF], CMD_HEARTBEAT)
        if error:
            self.add_test_result('TC_A0_004', '心跳帧边界值测试 - 幻数0xFF', False, '幻数=0xFF', None, error)
        else:
            magic = response['data'][0] if len(response['data']) > 0 else None
            passed = (magic == 0xFF)
            self.add_test_result('TC_A0_004', '心跳帧边界值测试 - 幻数0xFF', passed, '幻数=0xFF', f'幻数={magic:02X}', 
                              None if passed else "幻数不匹配")

    def test_fan_control(self):
        """测试风扇控制"""
        print("\n" + "="*60)
        print("测试组: 风扇控制 (0xA1)")
        print("="*60)
        
        test_cases = [
            ('TC_A1_001', '纯风扇编号模式 - 风扇1', 0x01, 50),
            ('TC_A1_002', '纯风扇编号模式 - 风扇2', 0x02, 100),
            ('TC_A1_003', '纯风扇编号模式 - 风扇3(排风扇)', 0x03, 75),
            ('TC_A1_004', '纯风扇编号模式 - 风扇7(热腔风扇)', 0x07, 80),
            ('TC_A1_005', '纯风扇编号模式 - 风扇11(鼓风机)', 0x0B, 60),
            ('TC_A1_006', '组模式 - 电气散热风扇组', 0x10, 90),
            ('TC_A1_007', '组模式 - 排风扇组', 0x11, 40),
            ('TC_A1_008', '组模式 - 热腔风扇组', 0x12, 70),
            ('TC_A1_009', '组模式 - 鼓风机组', 0x13, 55),
            ('TC_A1_010', '边界值测试 - 转速0%', 0x01, 0),
            ('TC_A1_011', '边界值测试 - 转速100%', 0x01, 100),
            ('TC_A1_012', '风扇ID边界值测试 - 风扇14', 0x0E, 50),
        ]
        
        for test_id, test_name, fan_id, speed in test_cases:
            print(f"\n[{test_id}] {test_name}")
            response, error = self.send_and_receive(CMD_SET_FAN, [fan_id, speed], CMD_SET_FAN)
            if error:
                self.add_test_result(test_id, test_name, False, f'风扇ID={fan_id:02X}, 结果=0x00', None, error)
            else:
                result_fan_id = response['data'][0] if len(response['data']) > 0 else None
                result_code = response['data'][1] if len(response['data']) > 1 else None
                passed = (result_fan_id == fan_id and result_code == 0x00)
                self.add_test_result(test_id, test_name, passed, 
                                  f'风扇ID={fan_id:02X}, 结果=0x00', 
                                  f'风扇ID={result_fan_id:02X}, 结果={result_code:02X}',
                                  None if passed else f"返回数据不匹配: 结果码={result_code:02X}")

    def test_fan_speed_query(self):
        """测试风扇转速查询"""
        print("\n" + "="*60)
        print("测试组: 风扇转速查询 (0xA2)")
        print("="*60)
        
        print("\n[TC_A2_001] 查询所有风扇转速")
        response, error = self.send_and_receive(CMD_FAN_GETSPEED, None, CMD_FAN_GETSPEED)
        if error:
            self.add_test_result('TC_A2_001', '查询所有风扇转速', False, '风扇数据(N*2字节)', None, error)
        else:
            # 验证数据格式：每个风扇2字节
            data_len = len(response['data'])
            passed = (data_len > 0 and data_len % 2 == 0)
            fan_data_str = ' '.join(f'{response["data"][i]:02X}{response["data"][i+1]:02X}' 
                                   for i in range(0, min(data_len, 20), 2))
            self.add_test_result('TC_A2_001', '查询所有风扇转速', passed, 
                              f'风扇数据(N*2字节)', 
                              f'数据长度={data_len}字节, 数据={fan_data_str}...',
                              None if passed else "数据格式错误")

    def test_fan_status_query(self):
        """测试风扇状态查询"""
        print("\n" + "="*60)
        print("测试组: 风扇状态查询 (0xA3)")
        print("="*60)
        
        print("\n[TC_A3_001] 查询所有风扇状态")
        response, error = self.send_and_receive(CMD_FAN_STATUS, None, CMD_FAN_STATUS)
        if error:
            self.add_test_result('TC_A3_001', '查询所有风扇状态', False, '风扇状态(N字节)', None, error)
        else:
            # 验证数据格式：每个风扇1字节
            data_len = len(response['data'])
            passed = (data_len > 0)
            status_str = ' '.join(f'{b:02X}' for b in response['data'][:20])
            self.add_test_result('TC_A3_001', '查询所有风扇状态', passed, 
                              f'风扇状态(N字节)', 
                              f'数据长度={data_len}字节, 状态={status_str}...',
                              None if passed else "数据格式错误")

    def test_color_light(self):
        """测试三色灯控制"""
        print("\n" + "="*60)
        print("测试组: 三色灯控制 (0xA4)")
        print("="*60)
        
        test_cases = [
            ('TC_A4_001', '绿色常亮', 0x00, 0x00),
            ('TC_A4_002', '黄色常亮', 0x01, 0x00),
            ('TC_A4_003', '红色常亮', 0x02, 0x00),
            ('TC_A4_004', '绿色慢速呼吸', 0x00, 0x01),
            ('TC_A4_005', '黄色中速呼吸', 0x01, 0x02),
            ('TC_A4_006', '红色快速呼吸', 0x02, 0x03),
            ('TC_A4_007', '绿色快速呼吸', 0x00, 0x03),
        ]
        
        for test_id, test_name, color, breath in test_cases:
            print(f"\n[{test_id}] {test_name}")
            response, error = self.send_and_receive(CMD_COLOR_LIGHT, [color, breath], CMD_COLOR_LIGHT)
            if error:
                self.add_test_result(test_id, test_name, False, 
                                  f'灯状态={color:02X}, 呼吸={breath:02X}', None, error)
            else:
                result_color = response['data'][0] if len(response['data']) > 0 else None
                result_breath = response['data'][1] if len(response['data']) > 1 else None
                passed = (result_color == color and result_breath == breath)
                self.add_test_result(test_id, test_name, passed, 
                                  f'灯状态={color:02X}, 呼吸={breath:02X}', 
                                  f'灯状态={result_color:02X}, 呼吸={result_breath:02X}',
                                  None if passed else "返回数据不匹配")

    def test_light_bar(self):
        """测试进度灯控制"""
        print("\n" + "="*60)
        print("测试组: 进度灯控制 (0xA5)")
        print("="*60)
        
        test_cases = [
            ('TC_A5_001', '全白色（进度0%）', 0x00, 0x00, 0x00),
            ('TC_A5_002', '进度50% - 红色', 0x32, 0x01, 0x00),
            ('TC_A5_003', '进度75% - 黄色', 0x4B, 0x02, 0x00),
            ('TC_A5_004', '进度100% - 绿色', 0x64, 0x04, 0x00),
            ('TC_A5_005', '进度25% - 蓝色慢速呼吸', 0x19, 0x03, 0x01),
            ('TC_A5_006', '进度10% - 自定义颜色', 0x0A, 0x41, 0x00),
            ('TC_A5_007', '进度99% - 蓝色中速呼吸', 0x63, 0x03, 0x02),
            ('TC_A5_008', '边界值测试 - 进度1%', 0x01, 0x00, 0x00),
        ]
        
        for test_id, test_name, progress, color, breath in test_cases:
            print(f"\n[{test_id}] {test_name}")
            response, error = self.send_and_receive(CMD_LIGHT_BAR, [progress, color, breath], CMD_LIGHT_BAR)
            if error:
                self.add_test_result(test_id, test_name, False, 
                                  f'进度={progress:02X}, 颜色={color:02X}, 呼吸={breath:02X}', None, error)
            else:
                result_progress = response['data'][0] if len(response['data']) > 0 else None
                result_color = response['data'][1] if len(response['data']) > 1 else None
                result_breath = response['data'][2] if len(response['data']) > 2 else None
                passed = (result_progress == progress and result_color == color and result_breath == breath)
                self.add_test_result(test_id, test_name, passed, 
                                  f'进度={progress:02X}, 颜色={color:02X}, 呼吸={breath:02X}', 
                                  f'进度={result_progress:02X}, 颜色={result_color:02X}, 呼吸={result_breath:02X}',
                                  None if passed else "返回数据不匹配")

    def test_all_status(self):
        """测试读取所有状态"""
        print("\n" + "="*60)
        print("测试组: 读取所有状态 (0xA6)")
        print("="*60)
        
        print("\n[TC_A6_001] 查询所有状态")
        response, error = self.send_and_receive(CMD_ALL_STATUS, None, CMD_ALL_STATUS)
        if error:
            self.add_test_result('TC_A6_001', '查询所有状态', False, '32字节数据', None, error)
        else:
            # 验证数据长度（期望32字节）
            data_len = len(response['data'])
            passed = (data_len == 32)
            temp_hex = ' '.join(f'{b:02X}' for b in response['data'][:20])
            self.add_test_result('TC_A6_001', '查询所有状态', passed, 
                              '32字节数据', 
                              f'数据长度={data_len}字节, 前20字节={temp_hex}...',
                              None if passed else f"数据长度错误: 期望32, 实际{data_len}")

    def test_temp_set(self):
        """测试设置目标温度"""
        print("\n" + "="*60)
        print("测试组: 设置目标温度 (0xA7)")
        print("="*60)
        
        test_cases = [
            ('TC_A7_001', '设置目标温度为50°C', 50),
            ('TC_A7_002', '设置目标温度为100°C', 100),
            ('TC_A7_003', '设置目标温度为150°C', 150),
            ('TC_A7_004', '边界值测试 - 温度0°C', 0),
            ('TC_A7_005', '边界值测试 - 温度255°C', 255),
        ]
        
        for test_id, test_name, temp in test_cases:
            print(f"\n[{test_id}] {test_name}")
            response, error = self.send_and_receive(CMD_TEMP_SET, [temp], CMD_TEMP_SET)
            if error:
                self.add_test_result(test_id, test_name, False, '设定结果=0x00', None, error)
            else:
                result_code = response['data'][0] if len(response['data']) > 0 else None
                passed = (result_code == 0x00)
                self.add_test_result(test_id, test_name, passed, 
                                  '设定结果=0x00', 
                                  f'设定结果={result_code:02X}',
                                  None if passed else f"设定结果错误: {result_code:02X}")

    def test_windows_control(self):
        """测试天窗控制"""
        print("\n" + "="*60)
        print("测试组: 天窗控制 (0xA8)")
        print("="*60)
        
        # TC_A8_001: 关闭天窗
        print("\n[TC_A8_001] 关闭天窗")
        response, error = self.send_and_receive(CMD_WINDOWS_CONTROL, [0x00], CMD_WINDOWS_CONTROL)
        if error:
            self.add_test_result('TC_A8_001', '关闭天窗', False, '天窗状态(1字节)', None, error)
        else:
            status = response['data'][0] if len(response['data']) > 0 else None
            passed = (status is not None)
            self.add_test_result('TC_A8_001', '关闭天窗', passed, 
                              '天窗状态(1字节)', 
                              f'天窗状态={status:02X}',
                              None if passed else "无返回数据")
        
        # TC_A8_002: 打开天窗
        print("\n[TC_A8_002] 打开天窗")
        response, error = self.send_and_receive(CMD_WINDOWS_CONTROL, [0x01], CMD_WINDOWS_CONTROL)
        if error:
            self.add_test_result('TC_A8_002', '打开天窗', False, '天窗状态(1字节)', None, error)
        else:
            status = response['data'][0] if len(response['data']) > 0 else None
            passed = (status is not None)
            self.add_test_result('TC_A8_002', '打开天窗', passed, 
                              '天窗状态(1字节)', 
                              f'天窗状态={status:02X}',
                              None if passed else "无返回数据")
        
        # TC_A8_003: 查询天窗状态
        print("\n[TC_A8_003] 查询天窗状态")
        response, error = self.send_and_receive(CMD_WINDOWS_CONTROL, [0x02], CMD_WINDOWS_CONTROL)
        if error:
            self.add_test_result('TC_A8_003', '查询天窗状态', False, '天窗状态(1字节)', None, error)
        else:
            status = response['data'][0] if len(response['data']) > 0 else None
            passed = (status is not None)
            self.add_test_result('TC_A8_003', '查询天窗状态', passed, 
                              '天窗状态(1字节)', 
                              f'天窗状态={status:02X}',
                              None if passed else "无返回数据")
        
        # TC_A8_004: 未知命令测试
        print("\n[TC_A8_004] 未知命令测试")
        response, error = self.send_and_receive(CMD_WINDOWS_CONTROL, [0x03], CMD_RESPONSE_ERROR)
        if error:
            self.add_test_result('TC_A8_004', '未知命令测试', False, 
                              '错误应答: 原命令码=0xA8, 错误码=0x01', None, error)
        else:
            passed = (response['cmd'] == CMD_RESPONSE_ERROR and 
                     len(response['data']) >= 2 and 
                     response['data'][0] == 0xA8 and 
                     response['data'][1] == 0x01)
            actual = f'原命令码={response["data"][0]:02X}, 错误码={response["data"][1]:02X}' if len(response['data']) >= 2 else '数据不足'
            self.add_test_result('TC_A8_004', '未知命令测试', passed, 
                              '错误应答: 原命令码=0xA8, 错误码=0x01', 
                              f'错误应答: {actual}',
                              None if passed else "错误应答格式错误")

    def test_error_response(self):
        """测试错误应答"""
        print("\n" + "="*60)
        print("测试组: 错误应答测试 (0xAE)")
        print("="*60)
        
        # TC_AE_001: 未知命令码测试
        print("\n[TC_AE_001] 未知命令码测试")
        response, error = self.send_and_receive(0xAF, None, CMD_RESPONSE_ERROR)
        if error:
            self.add_test_result('TC_AE_001', '未知命令码测试', False, 
                              '错误应答: 原命令码=0xAF, 错误码=0xFF', None, error)
        else:
            passed = (response['cmd'] == CMD_RESPONSE_ERROR and 
                     len(response['data']) >= 2 and 
                     response['data'][0] == 0xAF and 
                     response['data'][1] == 0xFF)
            actual = f'原命令码={response["data"][0]:02X}, 错误码={response["data"][1]:02X}' if len(response['data']) >= 2 else '数据不足'
            self.add_test_result('TC_AE_001', '未知命令码测试', passed, 
                              '错误应答: 原命令码=0xAF, 错误码=0xFF', 
                              f'错误应答: {actual}',
                              None if passed else "错误应答格式错误")
        
        # TC_CHECKSUM_001: 无效校验和测试
        print("\n[TC_CHECKSUM_001] 无效校验和测试")
        # 手动构建一个校验和错误的帧
        frame_data = bytes([FRAME_HEAD, 0x00, 0x07, CMD_HEARTBEAT, 0x12, 0xFF, FRAME_TAIL])
        
        print(f"  发送: {' '.join(f'{b:02X}' for b in frame_data)}")
        
        try:
            self.ser.reset_input_buffer()
            self.ser.write(frame_data)
            self.ser.flush()
            
            # 等待3秒，不应该有响应
            start_time = time.time()
            response = b''
            
            while time.time() - start_time < TIMEOUT:
                if self.ser.in_waiting > 0:
                    response += self.ser.read(self.ser.in_waiting)
                time.sleep(0.01)
            
            if response:
                print(f"  接收: {' '.join(f'{b:02X}' for b in response)}")
                passed = False
                error = "收到意外响应，帧应该被丢弃"
            else:
                print(f"  接收: (无响应)")
                passed = True
                error = None
            
            self.add_test_result('TC_CHECKSUM_001', '无效校验和测试', passed, 
                              '无响应（帧被丢弃）', 
                              '无响应' if not response else f'收到响应',
                              error)
            
        except Exception as e:
            self.add_test_result('TC_CHECKSUM_001', '无效校验和测试', False, 
                              '无响应（帧被丢弃）', None, f"通信异常: {str(e)}")

    def run_all_tests(self):
        """运行所有测试用例"""
        print("\n" + "="*60)
        print("开始协议测试")
        print("="*60)
        print(f"串口: {self.port}")
        print(f"波特率: {self.baudrate}")
        print(f"超时时间: {TIMEOUT}秒")
        print(f"测试时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        
        try:
            self.test_heartbeat()
            self.test_fan_control()
            self.test_fan_speed_query()
            self.test_fan_status_query()
            self.test_color_light()
            self.test_light_bar()
            self.test_all_status()
            self.test_temp_set()
            self.test_windows_control()
            self.test_error_response()
            
            # 统计结果
            total = len(self.test_results)
            passed = sum(1 for r in self.test_results if r['passed'])
            failed = total - passed
            pass_rate = (passed / total * 100) if total > 0 else 0
            
            print("\n" + "="*60)
            print("测试完成")
            print("="*60)
            print(f"总测试数: {total}")
            print(f"通过: {passed}")
            print(f"失败: {failed}")
            print(f"通过率: {pass_rate:.1f}%")
            
        except Exception as e:
            print(f"\n测试过程中发生异常: {e}")
            import traceback
            traceback.print_exc()

    def save_results_json(self, filename='test_results.json'):
        """保存测试结果为JSON格式"""
        with open(filename, 'w', encoding='utf-8') as f:
            json.dump(self.test_results, f, ensure_ascii=False, indent=2)
        print(f"\n✓ 测试结果已保存到: {filename}")

    def save_results_markdown(self, filename='test_report.md'):
        """保存测试结果为Markdown格式"""
        with open(filename, 'w', encoding='utf-8') as f:
            f.write("# 助手控制板通信协议测试报告\n\n")
            f.write(f"## 测试信息\n\n")
            f.write(f"- 测试时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"- 串口设备: {self.port}\n")
            f.write(f"- 波特率: {self.baudrate}\n")
            f.write(f"- 超时时间: {TIMEOUT}秒\n\n")
            
            # 统计
            total = len(self.test_results)
            passed = sum(1 for r in self.test_results if r['passed'])
            failed = total - passed
            pass_rate = (passed / total * 100) if total > 0 else 0
            
            f.write(f"## 测试统计\n\n")
            f.write(f"| 项目 | 数量 |\n")
            f.write(f"|------|------|\n")
            f.write(f"| 总测试数 | {total} |\n")
            f.write(f"| 通过 | {passed} |\n")
            f.write(f"| 失败 | {failed} |\n")
            f.write(f"| 通过率 | {pass_rate:.1f}% |\n\n")
            
            # 详细结果
            f.write(f"## 测试详情\n\n")
            
            for result in self.test_results:
                status = "✓ 通过" if result['passed'] else "✗ 失败"
                f.write(f"### [{result['test_id']}] {result['test_name']}\n\n")
                f.write(f"**状态:** {status}\n\n")
                f.write(f"**期望:** {result['expected']}\n\n")
                f.write(f"**实际:** {result['actual']}\n\n")
                if result['error']:
                    f.write(f"**错误:** {result['error']}\n\n")
                f.write(f"**时间:** {result['timestamp']}\n\n")
                f.write("---\n\n")
            
            # 失败用例汇总
            failed_cases = [r for r in self.test_results if not r['passed']]
            if failed_cases:
                f.write(f"## 失败用例汇总\n\n")
                for result in failed_cases:
                    f.write(f"- **{result['test_id']}** {result['test_name']}: {result['error']}\n")
                f.write("\n")
        
        print(f"✓ 测试报告已保存到: {filename}")


def main():
    # 创建测试实例
    tester = ProtocolTest(port='/dev/ttyS4', baudrate=115200)
    
    if not tester.ser or not tester.ser.is_open:
        print("✗ 无法连接到串口设备，测试终止")
        return 1
    
    try:
        # 运行所有测试
        tester.run_all_tests()
        
        # 保存结果
        tester.save_results_json('test_results.json')
        tester.save_results_markdown('test_report.md')
        
        return 0
        
    except KeyboardInterrupt:
        print("\n\n测试被用户中断")
        return 1
    except Exception as e:
        print(f"\n\n测试发生异常: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        tester.disconnect()


if __name__ == '__main__':
    sys.exit(main())
