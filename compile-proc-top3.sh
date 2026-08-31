#!/bin/sh
# Build the proc-top3 freestanding example.
page_size=4096

mkdir -p build/

gcc -nostdlib -static -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-builtin -O2 -s -Wl,--build-id=none -Wl,-z,noseparate-code \
    -Wl,-z,max-page-size=${page_size} \
    -Wall -Wextra proc-top3.c -o build/proc-top3
