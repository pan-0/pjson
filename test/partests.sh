#!/bin/bash
# Copyright 2025, pan (pan_@disroot.org)
# SPDX-License-Identifier: MIT-0 OR Apache-2.0

export ASAN_OPTIONS='detect_invalid_pointer_pairs=2:print_stacktrace=1:halt_on_error=1'
export UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1'

PATH=".:$PATH"

alias pj='pj -s0'

# Print which test files were skipped, i.e. those that start with an
# underscore ('_').
#
# skip(dir: Path)
skip ()
{
	find "$1" -name '_*.json' -exec printf '%-70s: SKIP\n' {} \;
}

# expect(status: int, file: Path)
expect ()
{
	pj "$2"
	status="$?"
	if [ "$status" -ne "$1" ]; then
		printf '%-70s: FAIL: got %s, expected %s\n' "$2" "$status" "$1"
	else
		printf '%-70s: PASS: got %s\n' "$2" "$status"
	fi
}

export -f expect

# JSONTestSuite.
#
# nst_tests(dir: Path)
nst_tests ()
{
	skip "$1"

	for file in "$1"/i_*.json; do
		pj "$file"
		printf '%-70s: IMPL: got %s\n' "$file" "$?"
	done

	find "$1" -name 'n_*.json' | parallel 'expect 1'
	find "$1" -name 'y_*.json' | parallel 'expect 0'
}

# JSON_checker.
#
# jsc_tests(dir: Path)
jsc_tests ()
{
	skip "$1"

	find "$1" -name 'fail*.json' | parallel 'expect 1'
	find "$1" -name 'pass*.json' | parallel 'expect 0'
}

main ()
{
	nst_tests 'nst'
	jsc_tests 'jsc'
	jsc_tests 'own'
}

main
