#!/usr/bin/env python3
import serial
import time

ser = serial.Serial('/dev/ttyS4', 115200, timeout=2)
ser.reset_input_buffer()
ser.reset_output_buffer()

# 发送心跳包: AE 00 07 A0 12 CHK FE
# 校验和 = AE + 00 + 07 + A0 + 12 = 0x167 -> 0x67
frame = bytes([0xAE, 0x00, 0x07, 0xA0, 0x12, 0x67, 0xFE])
print(f'发送: {frame.hex()}')
ser.write(frame)
ser.flush()

time.sleep(1)
response = ser.read(100)
print(f'接收: {response.hex() if response else "无响应"}')
print(f'接收长度: {len(response)}')

ser.close()
