#include "runtime.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_int8(int8_t i) {
  printf("%" PRId8, i);
}

void print_int16(int16_t i) {
  printf("%" PRId16, i);
}

void print_int32(int32_t i) {
  printf("%" PRId32, i);
}

void print_int64(int64_t i) {
  printf("%" PRId64, i);
}

void print_uint8(uint8_t i) {
  printf("%" PRIu8, i);
}

void print_uint16(uint16_t i) {
  printf("%" PRIu16, i);
}

void print_uint32(uint32_t i) {
  printf("%" PRIu32, i);
}

void print_uint64(uint64_t i) {
  printf("%" PRIu64, i);
}

void print_bool(bool b) {
  fputs(b ? "true" : "false", stdout);
}

void print_char(char c) {
  putchar(c);
}

void print_str(str s) {
  fwrite(s.data, 1, s.length, stdout);
}

dyn_arr rt_dyn_arr_make(uint64_t elem_size) {
  dyn_arr temp;
  temp.data = NULL;
  temp.length = 0;
  temp.capacity = 0;
  temp.elem_size = elem_size;
  return temp;
}

dyn_arr rt_dyn_arr_take(struct dyn_arr *arr) {
  dyn_arr temp = *arr;
  arr->data = NULL;
  arr->length = 0;
  arr->capacity = 0;
  return temp;
}

void rt_dyn_arr_push_back(struct dyn_arr *arr, void *elem) {
  if (arr->length + 1 > arr->capacity) {
    uint64_t new_cap = arr->capacity ? arr->capacity * 2 : 8;
    arr->data = realloc(arr->data, new_cap * arr->elem_size);
    arr->capacity = new_cap;
  }

  memcpy((char *)arr->data + arr->length * arr->elem_size, elem, arr->elem_size);
  arr->length++;
}

bool rt_str_equal(str a, str b) {
  if (a.length != b.length)
    return false;

  if (a.length == 0)
    return true;

  return memcmp(a.data, b.data, a.length) == 0;
}

string rt_str_concat(str a, str b) {
  string result;
  result.length = a.length + b.length;
  result.capacity = result.length;

  if (result.length == 0) {
    result.data = NULL;
    return result;
  }

  result.data = malloc(sizeof(char) * result.capacity);

  if (a.length != 0)
    memcpy(result.data, a.data, a.length);

  if (b.length != 0)
    memcpy(result.data + a.length, b.data, b.length);

  return result;
}

struct string string_make() {
  struct string s;
  s.data = NULL;
  s.length = 0;
  s.capacity = 0;
  return s;
}

struct string string_take(struct string *s) {
  struct string temp = *s;
  s->length = 0;
  s->capacity = 0;
  s->data = NULL;
  return temp;
}

void string_drop(struct string *s) {
  free(s->data);
}

void string_push_back_ptr_string_char(string *s, char c) {
  if (s->length + 1 > s->capacity) {
    uint64_t new_capacity = s->capacity == 0 ? 8 : s->capacity * 2;
    char *new_data = realloc(s->data, new_capacity);

    if (new_data == NULL)
      exit(EXIT_FAILURE);

    s->data = new_data;
    s->capacity = new_capacity;
  }

  s->data[s->length] = c;
  s->length += 1;
}

void string_push_back_ptr_string_str(string *s, str value) {
  if (s->length + value.length > s->capacity) {
    uint64_t new_capacity = s->capacity == 0 ? 8 : s->capacity;

    while (new_capacity < s->length + value.length)
      new_capacity *= 2;

    char *new_data = realloc(s->data, new_capacity);

    if (new_data == NULL)
      exit(EXIT_FAILURE);

    s->data = new_data;
    s->capacity = new_capacity;
  }

  memcpy(s->data + s->length, value.data, value.length);
  s->length += value.length;
}

void rt_assert(bool condition, str file, uint32_t line, uint32_t column) {
  if (condition)
    return;

  fprintf(stderr, "assertion failed at %.*s:%u:%u\n", (int)file.length, file.data, line, column);
  exit(1);
}

void rt_assert_msg(bool condition, str message, str file, uint32_t line, uint32_t column) {
  if (condition)
    return;

  fprintf(stderr, "assertion failed at %.*s:%u:%u: %.*s\n", (int)file.length, file.data, line, column, (int)message.length, message.data);
  exit(1);
}

void rt_panic(str file, uint32_t line, uint32_t column) {
  fprintf(stderr, "program panicked at %.*s:%u:%u\n", (int)file.length, file.data, line, column);
  exit(1);
}

void rt_panic_msg(str message, str file, uint32_t line, uint32_t column) {
  fprintf(stderr, "program panicked at %.*s:%u:%u: %.*s\n", (int)file.length, file.data, line, column, (int)message.length, message.data);
  exit(1);
}

string read_file(struct str filename) {
  char *null_terminated = malloc(filename.length + 1);
  assert(null_terminated != NULL);
  memcpy(null_terminated, filename.data, filename.length);
  null_terminated[filename.length] = '\0';

  FILE *f = fopen(null_terminated, "r");
  free(null_terminated);
  assert(f != NULL);

  fseek(f, 0, SEEK_END);
  uint64_t length = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *data = malloc(length);
  assert(data != NULL);

  fread(data, 1, length, f);
  fclose(f);

  struct string contents;
  contents.data = data;
  contents.length = length;
  contents.capacity = length;
  return contents;
}
