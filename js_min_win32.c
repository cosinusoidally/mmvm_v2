#include <windows.h>
#include <stdio.h>

HMODULE foo = 0;

int dlsym(int handle, char* symbol) {
  int d = 0;
  printf("looking up sym %s\n", symbol);
  if(foo == 0) {
    printf("setup foo\n");
    foo = GetModuleHandle("msvcrt.dll");
    if(foo == 0) {
      printf("can't get handle\n");
    }
  }

  d = GetProcAddress(foo, symbol);

  printf("symbol %s address %d\n", symbol, d);
/*
  if(d == (int)puts) {
    return (int)puts;
  }
  puts("dlsym not impl");
  exit(1);
*/

  return d;
}

#include "js_min.c"
