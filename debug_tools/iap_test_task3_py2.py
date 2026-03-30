#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
IAP upgrade test - Python2 compatible version
"""

import serial
import time
import struct
import sys
import os
import json
from datetime import datetime

# Protocol constants
FRAME_HEAD = 0xAE
FRAME_TAIL = 0xFE
TIMEOUT = 5.0
IAP_PACKET_TIMEOUT = 15.0
IAP_REQUEST_TIMEOUT = 60.0

# Command codes
CMD_HEARTBEAT = 0xA0
CMD_IAP_REQUEST = 0xAA
CMD_IAP_PACKET = 0xAB
CMD_VERSION_QUERY = 0xAC

# IAP parameters
IAP_PACKET_SIZE = 256
BOOT_DELAY = 5.0
RETRY_DELAY = 3.0
ERASE_DELAY = 15.0


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
            return True, "Connected"
        except Exception as e:
            return False, "Connect failed: " + str(e)

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def calculate_checksum(self, data):
        return sum([ord(c) if isinstance(c, str) else c for c in data]) & 0xFF

    def calculate_crc16(self, data):
        crc = 0xFFFF
        for b in data:
            byte = ord(b) if isinstance(b, str) else b
            crc ^= byte
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    def build_frame(self, cmd, data=None):
        if data is None:
            data = []
        frame_length = 1 + 2 + 1 + len(data) + 1 + 1
        
        # Build payload
        payload = chr(FRAME_HEAD) + chr((frame_length >> 8) & 0xFF) + chr(frame_length & 0xFF) + chr(cmd)
        for d in data:
            payload += chr(d)
        
        checksum = self.calculate_checksum(payload)
        return payload + chr(checksum) + chr(FRAME_TAIL)

    def recv_frame(self, timeout=TIMEOUT):
        start = time.time()
        buf = ''
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                buf += self.ser.read(self.ser.in_waiting)
                if chr(FRAME_HEAD) in buf:
                    idx = buf.index(chr(FRAME_HEAD))
                    buf = buf[idx:]
                    if len(buf) >= 3:
                        frame_len = (ord(buf[1]) << 8) | ord(buf[2])
                        if len(buf) >= frame_len:
                            return buf[:frame_len]
            time.sleep(0.01)
        return None

    def parse_frame(self, raw):
        if raw is None or len(raw) < 6:
            return None, "Response too short or timeout"
        if raw[0] != chr(FRAME_HEAD) or raw[-1] != chr(FRAME_TAIL):
            return None, "Frame head/tail error"
        frame_len = (ord(raw[1]) << 8) | ord(raw[2])
        if frame_len != len(raw):
            return None, "Length mismatch"
        calc_cs = self.calculate_checksum(raw[:-2])
        if calc_cs != ord(raw[-2]):
            return None, "Checksum error"
        cmd = ord(raw[3])
        data = [ord(c) for c in raw[4:-2]]
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
            return None, "Comm error: " + str(e)

    def send_heartbeat(self):
        resp, err = self.send_and_receive(CMD_HEARTBEAT, [0x12])
        if err:
            return False, err
        if resp['cmd'] == CMD_HEARTBEAT and len(resp['data']) >= 1 and resp['data'][0] == 0x12:
            return True, "Heartbeat OK"
        return False, "Magic mismatch"

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
            return None, "Command code mismatch"
        version = ''.join([chr(c) for c in resp['data']]).rstrip('\x00')
        return version, None

    def send_iap_request(self, firmware_size, total_packets, firmware_crc):
        # Big-endian
        data = []
        data.extend([(firmware_size >> 24) & 0xFF, (firmware_size >> 16) & 0xFF, 
                     (firmware_size >> 8) & 0xFF, firmware_size & 0xFF])
        data.extend([0x01, 0x00])  # Packet size = 256 (big-endian)
        data.extend([(total_packets >> 8) & 0xFF, total_packets & 0xFF])
        data.extend([(firmware_crc >> 8) & 0xFF, firmware_crc & 0xFF])
        
        print("  [IAP] Request frame data: " + ''.join(['%02X' % d for d in data]))
        
        resp, err = self.send_and_receive(CMD_IAP_REQUEST, data, timeout=IAP_REQUEST_TIMEOUT)
        if err:
            return None, err
        if resp['cmd'] != CMD_IAP_REQUEST:
            return None, "Response cmd error"
        result_code = resp['data'][0] if resp['data'] else 0xFF
        return result_code, None

    def send_iap_packet(self, seq, packet_data):
        data = [(seq >> 8) & 0xFF, seq & 0xFF]  # Big-endian seq
        data.extend([ord(c) if isinstance(c, str) else c for c in packet_data])
        
        resp, err = self.send_and_receive(CMD_IAP_PACKET, data, timeout=IAP_PACKET_TIMEOUT)
        if err:
            return None, err
        # Upgrade complete: MCU returns 0xAA, result=1
        if resp['cmd'] == CMD_IAP_REQUEST:
            result_code = resp['data'][0] if resp['data'] else 0xFF
            if result_code == 1:
                return 0xFFFF, None
            else:
                return None, "Upgrade failed, error code: " + str(result_code)
        # Normal: MCU returns 0xAB with next seq
        if resp['cmd'] == CMD_IAP_PACKET:
            if len(resp['data']) >= 2:
                next_seq = (resp['data'][0] << 8) | resp['data'][1]
                return next_seq, None
            return None, "Response data insufficient"
        return None, "Unknown response cmd"

    def do_upgrade(self, firmware_path, progress_callback=None):
        try:
            with open(firmware_path, 'rb') as f:
                firmware = f.read()
        except Exception as e:
            return False, "Failed to read firmware: " + str(e)

        firmware_size = len(firmware)
        total_packets = (firmware_size + IAP_PACKET_SIZE - 1) // IAP_PACKET_SIZE
        firmware_crc = self.calculate_crc16(firmware)
        padded = firmware + '\x00' * (total_packets * IAP_PACKET_SIZE - firmware_size)

        print("  Firmware size: %d bytes, %d packets, CRC16=0x%04X" % (firmware_size, total_packets, firmware_crc))

        result_code, err = self.send_iap_request(firmware_size, total_packets, firmware_crc)
        if err:
            return False, "IAP request failed: " + err
        if result_code != 0:
            return False, "MCU rejected upgrade, code: " + str(result_code)
        print("  [OK] MCU accepted upgrade, waiting for erase (%ds)..." % ERASE_DELAY)
        time.sleep(ERASE_DELAY)
        print("  [OK] Erase complete, starting transfer...")

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
                    return False, "Packet %d failed after max retries: " % seq + err
                print("  [WARN] Packet %d failed, retry %d/%d: " % (seq, retry_count, max_retry) + err)
                time.sleep(0.5)
                continue

            retry_count = 0
            if next_seq == 0xFFFF:
                if progress_callback:
                    progress_callback(total_packets, total_packets)
                return True, None

            if next_seq != seq + 1:
                print("  [WARN] MCU requests retransmit, seq=%d, expected=%d" % (seq, next_seq))
                seq = next_seq
                continue

            seq = next_seq
            if progress_callback:
                progress_callback(seq, total_packets)

        return True, None


def run_single_test(port, baudrate, firmware_path, test_num):
    print("\n" + "-" * 50)
    print("Test #%d starting" % test_num)
    print("-" * 50)

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
        print("  [%s] %s: %s" % ('OK' if ok else 'FAIL', name, msg))
        return ok

    tester = IAPTester(port, baudrate)

    ok, msg = tester.connect()
    if not step("Connect serial", ok, msg):
        result['error'] = msg
        return result

    try:
        ver, err = tester.query_version()
        if err:
            step("Query version before", False, err)
        else:
            result['version_before'] = ver
            step("Query version before", True, ver)

        progress_log = [0]
        def cb(cur, total):
            pct = cur * 100 // total
            if pct >= progress_log[0] + 10 or cur == total:
                print("    Progress: %d/%d (%d%%)" % (cur, total, pct))
                progress_log[0] = pct // 10 * 10

        ok, err = tester.do_upgrade(firmware_path, cb)
        if not step("Firmware transfer", ok, err if err else "Transfer complete, waiting for reboot"):
            result['error'] = err
            return result

        tester.disconnect()
        print("  Waiting for MCU reboot (%ds)..." % BOOT_DELAY)
        time.sleep(BOOT_DELAY)

        ok, msg = tester.connect()
        if not step("Reconnect", ok, msg):
            result['error'] = msg
            return result

        ready = tester.wait_for_ready(timeout=15.0)
        if not step("Wait for MCU ready", ready, "Ready" if ready else "Timeout"):
            result['error'] = "MCU no heartbeat after reboot"
            return result

        ver, err = tester.query_version()
        if err:
            step("Query version after", False, err)
            result['error'] = err
        else:
            result['version_after'] = ver
            step("Query version after", True, ver)
            result['success'] = True

    finally:
        tester.disconnect()

    return result


def main():
    import argparse
    parser = argparse.ArgumentParser(description='IAP Upgrade Test - Python2')
    parser.add_argument('--port', default='/dev/ttyS4', help='Serial port')
    parser.add_argument('--baudrate', type=int, default=115200)
    parser.add_argument('--firmware', required=True, help='Firmware bin file')
    parser.add_argument('--count', type=int, default=5, help='Test count')
    parser.add_argument('--output', default='/tmp/iap_test/iap_report', help='Report prefix')
    args = parser.parse_args()

    print("=" * 60)
    print("IAP Upgrade Test - Python2 Version")
    print("=" * 60)
    print("Serial: " + args.port)
    print("Firmware: " + args.firmware)
    print("Test count: %d" % args.count)
    print("=" * 60)

    if not os.path.exists(args.firmware):
        print("[FAIL] Firmware not found: " + args.firmware)
        sys.exit(1)

    firmware_size = os.path.getsize(args.firmware)
    print("Firmware size: %d bytes" % firmware_size)

    results = []
    for i in range(1, args.count + 1):
        r = run_single_test(args.port, args.baudrate, args.firmware, i)
        results.append(r)
        print("\nTest #%d: %s" % (i, 'Success' if r['success'] else 'Failed - ' + str(r['error'])))
        if not r['success'] and i < args.count:
            print("Waiting %ds..." % RETRY_DELAY)
            time.sleep(RETRY_DELAY)

    success_count = sum([1 for r in results if r['success']])
    fail_count = args.count - success_count
    success_rate = success_count * 100.0 / args.count

    print("\n" + "=" * 60)
    print("Test Summary")
    print("=" * 60)
    print("Total: %d" % args.count)
    print("Success: %d" % success_count)
    print("Failed: %d" % fail_count)
    print("Success rate: %.1f%%" % success_rate)

    for r in results:
        tag = "OK" if r['success'] else "FAIL"
        ver_info = "  Version: %s -> %s" % (r['version_before'], r['version_after']) if r['success'] else "  Error: " + str(r['error'])
        print("  [%s] Test #%d %s" % (tag, r['test_num'], ver_info))

    # Save report
    try:
        os.makedirs(os.path.dirname(args.output) if os.path.dirname(args.output) else '.', exist_ok=True)
    except:
        pass

    json_path = args.output + '.json'
    with open(json_path, 'w') as f:
        json.dump({
            'test_info': {
                'firmware': args.firmware,
                'firmware_size': firmware_size,
                'port': args.port,
                'baudrate': args.baudrate,
                'test_count': args.count,
                'test_time': datetime.now().isoformat(),
            },
            'summary': {
                'total': args.count,
                'success': success_count,
                'fail': fail_count,
                'success_rate': round(success_rate, 1),
            },
            'results': results,
        }, f, indent=2)
    print("\nJSON report: " + json_path)

    sys.exit(0 if success_count == args.count else 1)


if __name__ == '__main__':
    main()
