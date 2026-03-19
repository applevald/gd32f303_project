#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
根据测试结果更新protocol_test_cases.md文件
"""

import json
from datetime import datetime

def update_test_cases(test_results_file, test_cases_file, output_file):
    """根据测试结果更新测试用例文件"""

    # 读取测试结果
    with open(test_results_file, 'r', encoding='utf-8') as f:
        test_results = json.load(f)

    # 创建测试结果映射
    results_map = {r['test_id']: r for r in test_results}

    # 读取测试用例文件
    with open(test_cases_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 获取当前时间
    current_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    # 替换每个测试用例的结果
    lines = content.split('\n')
    in_result_section = False
    current_test_id = None

    output_lines = []

    for line in lines:
        if '| 用例ID |' in line and current_test_id is None:
            # 这是一个测试用例表头，开始跟踪
            in_result_section = True
            output_lines.append(line)
        elif in_result_section and line.startswith('| 用例ID |'):
            # 这是新测试用例的开始
            in_result_section = True
            output_lines.append(line)
        elif in_result_section and line.strip().startswith('| 用例ID'):
            # 提取测试ID
            parts = line.split('|')
            if len(parts) > 1:
                test_id = parts[1].strip()
                current_test_id = test_id
            output_lines.append(line)
        elif in_result_section and line.strip().startswith('| 测试结果'):
            # 替换测试结果行
            if current_test_id and current_test_id in results_map:
                result = results_map[current_test_id]
                if result['passed']:
                    # 通过
                    output_lines.append(f'| 测试结果 | [x] 通过 |')
                else:
                    # 失败
                    output_lines.append(f'| 测试结果 | [ ] 失败 |')
            else:
                output_lines.append(line)
        elif in_result_section and line.strip().startswith('| 失败原因'):
            # 替换失败原因行
            if current_test_id and current_test_id in results_map:
                result = results_map[current_test_id]
                if result['passed']:
                    output_lines.append(f'| 失败原因 | （如果失败，在此记录原因） |')
                else:
                    error_msg = result.get('error', '未知错误')
                    output_lines.append(f'| 失败原因 | {error_msg} (测试时间: {current_time}) |')
            else:
                output_lines.append(line)
        else:
            output_lines.append(line)

    # 写入输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))

    print(f"✓ 已更新测试用例文件: {output_file}")

if __name__ == '__main__':
    test_results_file = 'test_results.json'
    test_cases_file = 'gd32f303_project/doc/protocol_test_cases.md'
    output_file = 'gd32f303_project/doc/protocol_test_cases.md'

    update_test_cases(test_results_file, test_cases_file, output_file)