#include "json.h"
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

char key_pattern[128] = {0};
//char value_buffer[32] = {0};

void json_parser_init(json_parser_t *parser, char *json_str, int length)
{
    parser->json_str = json_str;
    parser->length = length;
}

static char *find_chr(char *str, char ch, int max_len)
{
    for (int i = 0; i < max_len; i++)
    {
        if (str[i] == ch)
            return &str[i];
        if (str[i] == '\0')
            return RT_NULL;
    }
    return RT_NULL;
}
/* 查找 JSON 键值对 */
static char* json_find_value(json_parser_t *parser, const char *key)
{
    rt_memset(key_pattern, 0, sizeof(key_pattern));
    /* 构建查找模式: "key": */
    rt_snprintf(key_pattern, sizeof(key_pattern), "\"%s\":", key);
    
    char *key_pos = rt_strstr(parser->json_str, key_pattern);
    if (!key_pos) return RT_NULL;
    
    /* 跳过键名和冒号 */
    char *value_pos = key_pos + rt_strlen(key_pattern);
    
    /* 跳过空格 */
    while (*value_pos == ' ' || *value_pos == '\t') value_pos++;
    
    return value_pos;
}

/* 提取字符串值 */
int json_get_string(json_parser_t *parser, const char *key, char *buffer, int buffer_size)
{
    char *value_pos = json_find_value(parser, key);
    if (!value_pos || *value_pos != '"') return -1;
    
    value_pos++;  // 跳过引号
    
    char *end_quote = find_chr(value_pos, '"', parser->length - (value_pos - parser->json_str));
    if (!end_quote) return -1;
    
    int value_len = end_quote - value_pos;
    if (value_len >= buffer_size) value_len = buffer_size - 1;
    
    rt_strncpy(buffer, value_pos, value_len);
    buffer[value_len] = '\0';
    
    return 0;
}

/* 提取整数值 */
int json_get_int(json_parser_t *parser, const char *key, int *value)
{
    char *value_pos = json_find_value(parser, key);
    if (!value_pos) return -1;
    
    *value = atoi(value_pos);
    return 0;
}

/* 提取浮点数值 */
int json_get_float(json_parser_t *parser, const char *key, float *value)
{
    char *value_pos = json_find_value(parser, key);
    if (!value_pos) return -1;
    
    *value = atof(value_pos);
    return 0;
}
