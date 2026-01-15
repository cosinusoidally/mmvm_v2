#include <windows.h>
#include <stdio.h>

HMODULE foo = 0;

int dlsym(int handle, char* symbol) {
  int d = 0;
  printf("looking up sym %s\n", symbol);
  if(foo) {
    printf("setup foo\n");
    foo = GetModuleHandle(0);
  }
  printf("symbol %s address %d\n", symbol, d);
  if(d == (int)puts) {
    return (int)puts;
  }
  puts("dlsym not impl");
  exit(1);
}

#include "js_min.c"
