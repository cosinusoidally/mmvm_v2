#include <windows.h>

HMODULE foo = 0;

int dlsym(int handle, char* symbol) {
  int d = 0;
  printf("looking up sym %s\n", symbol);
  if(foo) {
    printf("setup foo\n");
  } else {
    foo = 1;
    printf("symbol %s address %d\n", symbol, d);
  }
  puts("dlsym not impl");
  exit(1);
}

#include "js_min.c"
