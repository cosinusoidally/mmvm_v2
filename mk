./mk_clean

gcc -O0 -g -c -fno-stack-protector -Wall -Wno-format -DXP_UNIX -DSVR4 -DSYSV -D_BSD_SOURCE -DPOSIX_SOURCE -DHAVE_LOCALTIME_R -DHAVE_VA_COPY -DVA_COPY=va_copy js.c -o artifacts/js.o
