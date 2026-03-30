#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IAP升级功能测试脚本 - 任务3
用于测试MCU的固件升级功能，重复测试5次

协议字节序：大端序（MCU侧已按大端序解析）

使用方法:
    python3 iap_test_task3.py --port /dev/ttyS4 --firmware gd32f303.bin --count 5
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
IAP_PACKET_TIMEOUT = 45.0   # 每包写Flash最长等待（MCU擦除等待最多30秒）
IAP_REQUEST_TIMEOUT = 60.0  # 请求阶段（含擦除）最长等待

# 命令码
CMD_HEARTBEAT     = 0xA0
CMD_IAP_REQUEST   = 0xAA
CMD_IAP_PACKET    = 0xAB
CMD_VERSION_QUERY = 0xAC

# IAP参数
IAP_PACKET_SIZE = 256
BOOT_DELAY    = 5.0    # 等待Bootloader+APP启动
RETRY_DELAY   = 3.0    # 失败后重试等待
ERASE_DELAY   = 15.0   # 擦除等待时间（MCU先回复接受，然后异步擦除，需等待完成后再发包）


class IAPTester:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None

    def connect(self):
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
        if self.ser and self.ser.is_open:
            self.ser.close()

    def calculate_checksum(self, data):
        return sum(data) & 0xFF

    def calculate_crc16(self, data):
        """CRC16 Modbus"""
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
        payload = bytes([FRAME_HEAD,
                         (frame_length >> 8) & 0xFF,
                         frame_length & 0xFF,
                         cmd]) + bytes(data)
        checksum = self.calculate_checksum(payload)
        return payload + bytes([checksum, FRAME_TAIL])

    def recv_frame(self, timeout=TIMEOUT):
        """接收一帧，返回原始bytes或None"""
        start = time.time()
        buf = b''
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                buf += self.ser.read(self.ser.in_waiting)
                if FRAME_HEAD in buf:
                    idx = buf.index(FRAME_HEAD)
                    buf = buf[idx:]
                    if len(buf) >= 3:
                        frame_len = (buf[1] << 8) | buf[2]
                        if len(buf) >= frame_len:
                            return buf[:frame_len]
            time.sleep(0.01)
        return None

    def parse_frame(self, raw):
        if raw is None or len(raw) < 6:
            return None, "响应太短或超时"
        if raw[0] != FRAME_HEAD or raw[-1] != FRAME_TAIL:
            return None, f"帧头尾错误: {raw.hex()}"
        frame_len = (raw[1] << 8) | raw[2]
        if frame_len != len(raw):
            return None, f"长度不符: 期望{frame_len} 实际{len(raw)}"
        calc_cs = self.calculate_checksum(raw[:-2])
        if calc_cs != raw[-2]:
            return None, f"校验错误: 计算{calc_cs:02X} 实际{raw[-2]:02X}"
        cmd = raw[3]
        data = list(raw[4:-2])
        return {'cmd': cmd, 'data': data}, None

    def send_and_receive(self, cmd, data=None, timeout=TIMEOUT):
        frame = self.build_frame(cmd, data)
        try:
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            raw = self.recv_frame(timeout)
            return self.parse_frame(raw)
        except Exception as e:
            return None, f"通信异常: {e}"

    # ──────────────────────────────────────────────
    # 业务接口
    # ──────────────────────────────────────────────
    def send_heartbeat(self):
        magic = 0x12
        resp, err = self.send_and_receive(CMD_HEARTBEAT, [magic])
        if err:
            return False, err
        if resp['cmd'] == CMD_HEARTBEAT and len(resp['data']) >= 1 and resp['data'][0] == magic:
            return True, "心跳正常"
        return False, f"心跳幻数不匹配: {resp}"

    def wait_for_ready(self, timeout=15.0):
        start = time.time()
        while time.time() - start < timeout:
            ok, _ = self.send_heartbeat()
            if ok:
                return True
            time.sleep(0.5)
        return False

    def query_version(self):
        resp, err = self.send_and_receive(CMD_VERSION_QUERY, timeout=TIMEOUT)
        if err:
            return None, err
        if resp['cmd'] != CMD_VERSION_QUERY:
            return None, f"命令码不匹配: {resp['cmd']:02X}"
        version = bytes(resp['data']).rstrip(b'\x00').decode('ascii', errors='replace')
        return version, None

    def send_iap_request(self, firmware_size, total_packets, firmware_crc):
        """
        IAP请求帧有效数据区（大端序）：
          firmware_size  4字节
          packet_size    2字节  固定0x0100=256
          total_packets  2字节
          firmware_crc   2字节
        """
        data = bytearray()
        data.extend(struct.pack('>I', firmware_size))    # 大端 4字节
        data.extend(struct.pack('>H', IAP_PACKET_SIZE))  # 大端 2字节 = 0x01 0x00
        data.extend(struct.pack('>H', total_packets))    # 大端 2字节
        data.extend(struct.pack('>H', firmware_crc))     # 大端 2字节
        print(f"  [IAP] 请求帧数据: {data.hex()}")
        resp, err = self.send_and_receive(CMD_IAP_REQUEST, data, timeout=IAP_REQUEST_TIMEOUT)
        if err:
            return None, err
        if resp['cmd'] != CMD_IAP_REQUEST:
            return None, f"响应命令码错误: 0x{resp['cmd']:02X}"
        result_code = resp['data'][0] if resp['data'] else 0xFF
        return result_code, None

    def send_iap_packet(self, seq, packet_data):
        """
        数据包有效数据区：
          seq    2字节 大端序
          data   256字节
        返回：
          (next_seq, None)   正常，next_seq为期望下一包
          (0xFFFF, None)     升级完成
          (None, error_msg)  失败
        """
        data = bytearray()
        data.extend(struct.pack('>H', seq))  # 大端 2字节
        data.extend(packet_data)
        resp, err = self.send_and_receive(CMD_IAP_PACKET, data, timeout=IAP_PACKET_TIMEOUT)
        if err:
            return None, err
        # 升级完成：MCU回 0xAA，result=1
        if resp['cmd'] == CMD_IAP_REQUEST:
            result_code = resp['data'][0] if resp['data'] else 0xFF
            if result_code == 1:
                return 0xFFFF, None
            else:
                return None, f"升级失败，MCU返回错误码: {result_code}"
        # 正常请求下一包：MCU回 0xAB，2字节大端序序列号
        if resp['cmd'] == CMD_IAP_PACKET:
            if len(resp['data']) >= 2:
                next_seq = struct.unpack('>H', bytes(resp['data'][:2]))[0]
                return next_seq, None
            return None, f"响应数据不足: {bytes(resp['data']).hex()}"
        return None, f"未知响应命令码: 0x{resp['cmd']:02X}"

    def do_upgrade(self, firmware_path, progress_callback=None):
        try:
            with open(firmware_path, 'rb') as f:
                firmware = f.read()
        except Exception as e:
            return False, f"读取固件失败: {e}"

        firmware_size = len(firmware)
        total_packets = (firmware_size + IAP_PACKET_SIZE - 1) // IAP_PACKET_SIZE
        firmware_crc  = self.calculate_crc16(firmware)
        padded = firmware + b'\x00' * (total_packets * IAP_PACKET_SIZE - firmware_size)

        print(f"  固件大小: {firmware_size} 字节，共 {total_packets} 包，CRC16=0x{firmware_crc:04X}")

        result_code, err = self.send_iap_request(firmware_size, total_packets, firmware_crc)
        if err:
            return False, f"IAP请求失败: {err}"
        if result_code != 0:
            return False, f"MCU拒绝升级，结果码: {result_code}"
        print(f"  [OK] MCU已接受升级请求，等待擦除完成({ERASE_DELAY}s)...")
        time.sleep(ERASE_DELAY)   # MCU先回复接受，随后异步执行擦除，必须等擦除完才能发包
        print("  [OK] 擦除等待完毕，开始传输...")

        seq = 0
        retry_count = 0
        max_retry = 3
        while seq < total_packets:
            start = seq * IAP_PACKET_SIZE
            packet = padded[start:start + IAP_PACKET_SIZE]

            next_seq, err = self.send_iap_packet(seq, packet)
            if err:
                retry_count += 1
                if retry_count > max_retry:
                    return False, f"包 {seq} 失败超过最大重试次数: {err}"
                print(f"  [WARN] 包 {seq} 失败，重试 {retry_count}/{max_retry}: {err}")
                time.sleep(0.5)
                continue

            retry_count = 0
            if next_seq == 0xFFFF:
                if progress_callback:
                    progress_callback(total_packets, total_packets)
                return True, None

            if next_seq != seq + 1:
                print(f"  [WARN] MCU要求重传，当前seq={seq}，MCU期望seq={next_seq}")
                seq = next_seq
                continue

            seq = next_seq
            if progress_callback:
                progress_callback(seq, total_packets)

        return True, None


# ──────────────────────────────────────────────────────
# 单次测试
# ──────────────────────────────────────────────────────
def run_single_test(port, baudrate, firmware_path, test_num):
    print(f"\n{'─'*50}")
    print(f"第 {test_num} 次测试开始")
    print(f"{'─'*50}")

    result = {
        'test_num': test_num,
        'timestamp': datetime.now().isoformat(),
        'steps': [],
        'success': False,
        'error': None,
        'version_before': None,
        'version_after': None,
    }

    def step(name, ok, msg=''):
        result['steps'].append({'step': name, 'success': ok, 'message': msg})
        print(f"  [{'OK' if ok else 'FAIL'}] {name}: {msg}")
        return ok

    tester = IAPTester(port, baudrate)

    ok, msg = tester.connect()
    if not step("连接串口", ok, msg):
        result['error'] = msg
        return result

    try:
        ver, err = tester.query_version()
        if err:
            step("查询升级前版本", False, err)
        else:
            result['version_before'] = ver
            step("查询升级前版本", True, ver)

        progress_log = [0]
        def cb(cur, total):
            pct = cur * 100 // total
            if pct >= progress_log[0] + 10 or cur == total:
                print(f"    传输进度: {cur}/{total} ({pct}%)")
                progress_log[0] = pct // 10 * 10

        ok, err = tester.do_upgrade(firmware_path, cb)
        if not step("固件传输", ok, err if err else "传输完成，等待重启"):
            result['error'] = err
            return result

        tester.disconnect()
        print(f"  等待MCU重启 ({BOOT_DELAY}s)...")
        time.sleep(BOOT_DELAY)

        ok, msg = tester.connect()
        if not step("重新连接", ok, msg):
            result['error'] = msg
            return result

        ready = tester.wait_for_ready(timeout=15.0)
        if not step("等待MCU心跳就绪", ready, "就绪" if ready else "超时无响应"):
            result['error'] = "MCU启动后无心跳响应"
            return result

        ver, err = tester.query_version()
        if err:
            step("查询升级后版本", False, err)
            result['error'] = err
        else:
            result['version_after'] = ver
            step("查询升级后版本", True, ver)
            result['success'] = True

    finally:
        tester.disconnect()

    return result


# ──────────────────────────────────────────────────────
# 主函数
# ──────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description='IAP升级功能测试 - 任务3（大端序）')
    parser.add_argument('--port',     default='/dev/ttyS4', help='串口设备')
    parser.add_argument('--baudrate', type=int, default=115200)
    parser.add_argument('--firmware', required=True, help='固件bin文件路径')
    parser.add_argument('--count',    type=int, default=5, help='测试次数')
    parser.add_argument('--output',   default='/tmp/iap_test/iap_report', help='报告文件前缀(不含扩展名)')
    args = parser.parse_args()

    print("=" * 60)
    print("IAP升级功能测试 - 任务3")
    print("=" * 60)
    print(f"串口     : {args.port}")
    print(f"固件     : {args.firmware}")
    print(f"测试次数 : {args.count}")
    print(f"字节序   : 大端序 (Big-Endian)")
    print("=" * 60)

    if not os.path.exists(args.firmware):
        print(f"[FAIL] 固件文件不存在: {args.firmware}")
        sys.exit(1)

    firmware_size = os.path.getsize(args.firmware)
    print(f"固件大小: {firmware_size} 字节")

    results = []
    for i in range(1, args.count + 1):
        r = run_single_test(args.port, args.baudrate, args.firmware, i)
        results.append(r)
        print(f"\n第 {i} 次: {'成功' if r['success'] else '失败 - ' + str(r['error'])}")
        if not r['success'] and i < args.count:
            print(f"等待 {RETRY_DELAY}s 后继续...")
            time.sleep(RETRY_DELAY)

    success_count = sum(1 for r in results if r['success'])
    fail_count    = args.count - success_count
    success_rate  = success_count * 100 / args.count

    print("\n" + "=" * 60)
    print("测试汇总")
    print("=" * 60)
    print(f"总次数  : {args.count}")
    print(f"成功    : {success_count}")
    print(f"失败    : {fail_count}")
    print(f"成功率  : {success_rate:.1f}%")
    for r in results:
        tag = "√" if r['success'] else "×"
        ver_info = f"  版本: {r['version_before']} -> {r['version_after']}" if r['success'] else f"  错误: {r['error']}"
        print(f"  [{tag}] 第{r['test_num']}次{ver_info}")

    # JSON报告
    report = {
        'test_info': {
            'firmware': args.firmware,
            'firmware_size': firmware_size,
            'port': args.port,
            'baudrate': args.baudrate,
            'test_count': args.count,
            'byte_order': 'big-endian',
            'test_time': datetime.now().isoformat(),
        },
        'summary': {
            'total': args.count,
            'success': success_count,
            'fail': fail_count,
            'success_rate': round(success_rate, 1),
        },
        'results': results,
    }
    os.makedirs(os.path.dirname(args.output) if os.path.dirname(args.output) else '.', exist_ok=True)
    json_path = args.output + '.json'
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    print(f"\nJSON报告: {json_path}")

    # Markdown报告
    lines = [
        "# IAP升级功能测试报告（任务3）", "",
        "## 测试信息", "",
        "| 项目 | 值 |", "|------|----|",
        f"| 固件文件 | `{os.path.basename(args.firmware)}` |",
        f"| 固件大小 | {firmware_size} 字节 |",
        f"| 串口 | {args.port} |",
        f"| 波特率 | {args.baudrate} |",
        f"| 字节序 | 大端序 |",
        f"| 测试时间 | {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} |",
        "", "## 测试结果摘要", "",
        "| 总次数 | 成功 | 失败 | 成功率 |",
        "|--------|------|------|--------|",
        f"| {args.count} | {success_count} | {fail_count} | {success_rate:.1f}% |",
        "", "## 详细结果", "",
    ]
    for r in results:
        tag = "成功" if r['success'] else f"失败: {r['error']}"
        lines.append(f"### 第 {r['test_num']} 次：{tag}")
        lines.append("")
        if r['version_before'] or r['version_after']:
            lines.append(f"- 升级前版本：`{r['version_before']}`")
            lines.append(f"- 升级后版本：`{r['version_after']}`")
            lines.append("")
        lines.append("| 步骤 | 结果 | 说明 |")
        lines.append("|------|------|------|")
        for s in r['steps']:
            lines.append(f"| {s['step']} | {'OK' if s['success'] else 'FAIL'} | {s['message']} |")
        lines.append("")

    md_path = args.output + '.md'
    with open(md_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f"MD报告  : {md_path}")

    sys.exit(0 if success_count == args.count else 1)


if __name__ == '__main__':
    main()