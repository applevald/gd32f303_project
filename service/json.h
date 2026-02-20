#ifndef _JSON_H_
#define _JSON_H_

typedef struct {
    char *json_str;
    int length;
} json_parser_t;

void json_parser_init(json_parser_t *parser, char *json_str, int length);
int json_get_int(json_parser_t *parser, const char *key, int *value);
int json_get_float(json_parser_t *parser, const char *key, float *value);
int json_get_string(json_parser_t *parser, const char *key, char *buffer, int buffer_size);

#endif /* _JSON_H_ */
