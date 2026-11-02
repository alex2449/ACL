#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

typedef struct _slice_str {
  str *data;
  uint64_t length;
} _slice_str;

extern int32_t acl_main(_slice_str args);

int main(int argc, char **argv) {
  str *data = malloc(sizeof(str) * argc);

  for (int i = 0; i < argc; i++) {
    data[i].length = strlen(argv[i]);
    data[i].data = argv[i];
  }

  _slice_str args;
  args.data = data;
  args.length = argc;

  int32_t result = acl_main(args);

  free(data);

  return result;
}
