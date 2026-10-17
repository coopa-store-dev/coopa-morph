# tests/Makefile —— 宿主测试(纯 C 的部分)。make -C tests:全部编译(AddressSanitizer + UBSan)并运行。
CC ?= cc
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
          -I../src -I.
OUT := .build
HDRS := $(wildcard ../src/*.h) $(wildcard *.h)
TESTS := test_level test_body test_play test_keys test_art

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	@echo "host tests: PASS"

$(OUT):
	mkdir -p $@

$(OUT)/test_level: test_level.c ../src/morph_level.c ../src/morph_levels.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_body: test_body.c ../src/morph_body.c ../src/morph_level.c ../src/morph_levels.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# 两个 morph_play_t 经假管道(duo.c)联机
$(OUT)/test_play: test_play.c duo.c ../src/morph_play.c ../src/morph_body.c ../src/morph_level.c ../src/morph_levels.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_keys: test_keys.c ../src/morph_keys.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# 常量图:宿主上没有 LVGL,用 stub/lvgl.h(只有 lv_image_dsc_t)
$(OUT)/test_art: test_art.c ../src/morph_art.c $(HDRS) stub/lvgl.h | $(OUT)
	$(CC) $(CFLAGS) -Istub $(filter %.c,$^) -o $@

.PHONY: all
