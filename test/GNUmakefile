# Copyright 2025, pan (pan_@disroot.org)
# SPDX-License-Identifier: MIT-0 OR Apache-2.0

.POSIX:
.SUFFIXES:

CC    ::= c99
BUILD ::= default

default.cflags ::= -O2
sanitize.cflags ::= \
	-g \
	-O3 \
	-fsanitize=address,leak,undefined

CFLAGS ::= \
	$($(BUILD).cflags) \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Werror \
	-Wno-unused-function \
	-Wno-missing-field-initializers
CPPFLAGS ::= -I..

RUNTESTS ::= runtests.sh

VPATH ::= ..

all: pj
.PHONY: all

pj: pj.c pjson.c pjson.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@

sh_check: runtests.sh partests.sh
	@shellcheck $^
.PHONY: sh_check

tests: $(RUNTESTS) pj
	@!(./$< | tee /dev/stderr | grep 'FAIL' >/dev/null)
.PHONY: tests

clean:
	@rm -f pj
.PHONY: clean
