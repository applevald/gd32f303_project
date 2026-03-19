#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
更新测试用例文件
根据测试结果更新protocol_test_cases.md文件
"""

import json
import re
from datetime import datetime

# 文件路径
TEST_CASES_FILE = r"D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\protocol_test_cases.md"
TEST_RESULTS_FILE = r"D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\test_results.json"
OUTPUT_FILE = r"D:\Users\admin\Desktop\new_file\gd32f303_project\gd32f303_project\doc\protocol_test_cases_updated.md"


def load_test_results():
    """加载测试结果"""
    with open(TEST_RESULTS_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)


def update_test_cases(test_cases_content, test_results):
    """更新测试用例内容"""
    lines = test_cases_content.split('\n')
    updated_lines = []
    
    current_test_id = None
    result_line_found = False
    
    for line in lines:
        # 检查是否是用例ID行
        match = re.match(r'^\| 用例ID \| (.+) \|$', line)
        if match:
            current_test_id = match.group(1)
            result_line_found = False
            updated_lines.append(line)
            continue
        
        # 检查是否是测试结果行
        if current_test_id and re.match(r'^\| 测试结果 \|', line):
            # 查找对应的测试结果
            test_result = None
            for result in test_results:
                if result['test_id'] == current_test_id:
                    test_result = result
                    break
            
            if test_result:
                if test_result['passed']:
                    # 通过
                    updated_line = '| 测试结果 | [x] 通过 |'
                else:
                    # 失败
                    error = test_result.get('error', '未知错误')
                    updated_line = f'| 测试结果 | [ ] 失败 |'
                updated_lines.append(updated_line)
                
                # 更新失败原因行（如果是下一行）
                result_line_found = True
            else:
                updated_lines.append(line)
            continue
        
        # 检查是否是失败原因行
        if result_line_found and re.match(r'^\| 失败原因 \|', line):
            test_result = None
            for result in test_results:
                if result['test_id'] == current_test_id:
                    test_result = result
                    break
            
            if test_result and not test_result['passed']:
                error = test_result.get('error', '未知错误')
                timestamp = test_result.get('timestamp', '')
                updated_line = f'| 失败原因 | {error} (测试时间: {timestamp}) |'
                updated_lines.append(updated_line)
                result_line_found = False
                continue
            else:
                updated_lines.append(line)
                result_line_found = False
                continue
        
        updated_lines.append(line)
    
    # 更新测试统计表格
    # 统计结果
    total = len(test_results)
    passed = sum(1 for r in test_results if r['passed'])
    failed = total - passed
    pass_rate = (passed / total * 100) if total > 0 else 0
    
    # 查找并更新统计表格
    final_lines = []
    in_stats_table = False
    
    for line in updated_lines:
        if re.match(r'^## 测试统计$', line):
            in_stats_table = True
            final_lines.append(line)
            continue
        
        if in_stats_table and re.match(r'^\| 总计 \|', line):
            # 添加更新后的统计表格
            final_lines.append('| 协议类型 | 测试用例数 | 通过 | 失败 | 通过率 |')
            final_lines.append('|---------|-----------|------|------|--------|')
            
            # 按协议类型分组统计
            protocol_stats = {}
            for result in test_results:
                # 从测试ID提取协议类型
                if result['test_id'].startswith('TC_A0'):
                    protocol = '心跳帧 (0xA0)'
                elif result['test_id'].startswith('TC_A1'):
                    protocol = '风扇控制 (0xA1)'
                elif result['test_id'].startswith('TC_A2'):
                    protocol = '风扇转速查询 (0xA2)'
                elif result['test_id'].startswith('TC_A3'):
                    protocol = '风扇状态查询 (0xA3)'
                elif result['test_id'].startswith('TC_A4'):
                    protocol = '三色灯控制 (0xA4)'
                elif result['test_id'].startswith('TC_A5'):
                    protocol = '进度灯控制 (0xA5)'
                elif result['test_id'].startswith('TC_A6'):
                    protocol = '读取所有状态 (0xA6)'
                elif result['test_id'].startswith('TC_A7'):
                    protocol = '设置目标温度 (0xA7)'
                elif result['test_id'].startswith('TC_A8'):
                    protocol = '天窗控制 (0xA8)'
                elif result['test_id'].startswith('TC_AE') or result['test_id'].startswith('TC_CHECKSUM'):
                    protocol = '错误应答测试'
                else:
                    protocol = '其他'
                
                if protocol not in protocol_stats:
                    protocol_stats[protocol] = {'total': 0, 'passed': 0, 'failed': 0}
                
                protocol_stats[protocol]['total'] += 1
                if result['passed']:
                    protocol_stats[protocol]['passed'] += 1
                else:
                    protocol_stats[protocol]['failed'] += 1
            
            # 输出协议统计
            for protocol, stats in sorted(protocol_stats.items()):
                protocol_pass_rate = (stats['passed'] / stats['total'] * 100) if stats['total'] > 0 else 0
                final_lines.append(f'| {protocol} | {stats["total"]} | {stats["passed"]} | {stats["failed"]} | {protocol_pass_rate:.1f}% |')
            
            # 输出总计
            final_lines.append('| **总计** | **{}** | **{}** | **{}** | **{:.1f}%** |'.format(
                total, passed, failed, pass_rate))
            
            in_stats_table = False
            continue
        
        # 跳过旧的统计表格行
        if in_stats_table and line.startswith('|'):
            continue
        
        final_lines.append(line)
    
    return '\n'.join(final_lines)


def main():
    print("=" * 60)
    print("更新测试用例文件")
    print("=" * 60)
    
    # 加载测试结果
    print("\n[步骤1] 加载测试结果...")
    test_results = load_test_results()
    print(f"✓ 加载了 {len(test_results)} 个测试结果")
    
    # 读取测试用例文件
    print("\n[步骤2] 读取测试用例文件...")
    with open(TEST_CASES_FILE, 'r', encoding='utf-8') as f:
        test_cases_content = f.read()
    print("✓ 读取成功")
    
    # 更新测试用例
    print("\n[步骤3] 更新测试用例...")
    updated_content = update_test_cases(test_cases_content, test_results)
    print("✓ 更新完成")
    
    # 保存更新后的文件
    print("\n[步骤4] 保存更新后的文件...")
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write(updated_content)
    print(f"✓ 已保存到: {OUTPUT_FILE}")
    
    # 统计
    total = len(test_results)
    passed = sum(1 for r in test_results if r['passed'])
    failed = total - passed
    pass_rate = (passed / total * 100) if total > 0 else 0
    
    print("\n" + "=" * 60)
    print("更新完成")
    print("=" * 60)
    print(f"总测试数: {total}")
    print(f"通过: {passed}")
    print(f"失败: {failed}")
    print(f"通过率: {pass_rate:.1f}%")
    print("")
    
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())