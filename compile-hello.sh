#!/bin/bash
# Set your page size variable
page_size=4096

# Make sure binary dir exists
mkdir -p build/

# Compile using your exact tuxreaperd pipeline
gcc -nostdlib -static -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-builtin -O2 -s -Wl,--build-id=none -Wl,-z,noseparate-code \
    -Wl,-z,max-page-size=${page_size} \
    -Wall -Wextra hello-world.c -o build/hello-world

