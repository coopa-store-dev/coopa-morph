# tests/Makefile —— 宿主测试(纯 C 的部分)。make -C tests:全部编译(AddressSanitizer + UBSan)并运行。
CC ?= cc
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
          -I../src -I.
OUT := .build
HDRS := $(wildcard ../src/*.h) $(wildcard *.h)
TESTS := test_run test_net test_art

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	@echo "host tests: PASS"

$(OUT):
	mkdir -p $@

# 常量图:宿主上没有 LVGL,用 stub/lvgl.h(只有 lv_image_dsc_t)
$(OUT)/test_art: test_art.c ../src/morph_art.c $(HDRS) stub/lvgl.h | $(OUT)
	$(CC) $(CFLAGS) -Istub $(filter %.c,$^) -o $@

# 一趟冒险:乱按的人一个人 / 两个人打很多趟,加几条规则单测
$(OUT)/test_run: test_run.c ../src/morph_run.c ../src/morph_battle.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -lm -o $@

# 两张卡延迟锁步:两份 morph_net_t 经假链路打完整趟(开优化、不开 sanitizer 跑整趟,另编一个 ASan 版跑一样的)
$(OUT)/test_net: test_net.c ../src/morph_net.c ../src/morph_run.c ../src/morph_battle.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -lm -o $@

.PHONY: all

