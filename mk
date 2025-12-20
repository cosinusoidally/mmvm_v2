./mk_clean

set -xe

gcc -O0 -g -c -fno-stack-protector -Wall -Wno-format -DXP_UNIX -DSVR4 -DSYSV -D_BSD_SOURCE -DPOSIX_SOURCE -DHAVE_LOCALTIME_R -DHAVE_VA_COPY -DVA_COPY=va_copy -I. -I ../firefox-1.0.8/js_src/src/ js.c -o artifacts/js.o

gcc artifacts/js.o -L../firefox-1.0.8/lib/ -lmozjs -o artifacts/js.exe -ldl

ldd artifacts/js.exe

artifacts/js.exe mandel.js

echo DONE
