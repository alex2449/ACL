#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct dyn_arr {
    void *data;
    uint64_t length;
    uint64_t capacity;
    uint64_t elem_size;
} dyn_arr;

typedef struct string {
    char *data;
    uint64_t length;
    uint64_t capacity;
} string;

typedef struct str {
    char *data;
    uint64_t length;
} str;

void print_int8(int8_t i);
void print_int16(int16_t i);
void print_int32(int32_t i);
void print_int64(int64_t i);
void print_uint8(uint8_t i);
void print_uint16(uint16_t i);
void print_uint32(uint32_t i);
void print_uint64(uint64_t i);
void print_bool(bool b);
void print_char(char c);
void print_str(str s);

dyn_arr rt_dyn_arr_make(uint64_t elem_size);
dyn_arr rt_dyn_arr_take(struct dyn_arr *arr);
void rt_dyn_arr_push_back(struct dyn_arr *arr, void *elem);

bool rt_str_equal(str a, str b);
string rt_str_concat(str a, str b);

struct string string_make();
struct string string_take(struct string *s);
void string_drop(struct string *s);
void string_push_back_ptr_string_char(string *s, char c);
void string_push_back_ptr_string_str(string *s, str value);

void rt_assert(bool condition, str file, uint32_t line, uint32_t column);
void rt_assert_msg(bool condition, str message, str file, uint32_t line, uint32_t column);

void rt_panic(str file, uint32_t line, uint32_t column);
void rt_panic_msg(str message, str file, uint32_t line, uint32_t column);

// _Noreturn void rt_exit(int32_t status);

string read_file(str filename);

#endif
