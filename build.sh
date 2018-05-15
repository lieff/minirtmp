gcc -Os -s -fno-asynchronous-unwind-tables -fno-stack-protector -ffunction-sections -fdata-sections \
-Wl,--gc-sections -DNDEBUG -o minirtmp librtmp/*.c minirtmp.c minirtmp_test.c system.cpp -lpthread