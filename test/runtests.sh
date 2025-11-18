#!/bin/sh
# Copyright 2025, pan (pan_@disroot.org)
# SPDX-License-Identifier: MIT-0 OR Apache-2.0

export ASAN_OPTIONS='detect_invalid_pointer_pairs=2:print_stacktrace=1:halt_on_error=1'
export UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1'

alias pj='./pj -s0'

# Print which test files were skipped, i.e. those that start with an
# underscore ('_').
#
# skip(dir: Path)
skip ()
{
	find "$1" -name '_*.json' -exec printf '%-70s: SKIP\n' {} \;
}

# expect(file: Path, status: int)
expect ()
{
	status="$?"
	if [ "$status" -ne "$2" ]; then
		printf '%-70s: FAIL: got %s, expected %s\n' "$file" "$status" "$2"
	else
		printf '%-70s: PASS: got %s\n' "$file" "$status"
	fi
}

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

	for file in "$1"/n_*.json; do
		pj "$file"
		expect "$file" 1
	done

	for file in "$1"/y_*.json; do
		pj "$file"
		expect "$file" 0
	done
}

# JSON_checker.
#
# jsc_tests(dir: Path)
jsc_tests ()
{
	skip "$1"

	for file in "$1"/fail*.json; do
		pj "$file"
		expect "$file" 1
	done

	for file in "$1"/pass*.json; do
		pj "$file"
		expect "$file" 0
	done
}

main ()
{
	nst_tests 'nst'
	jsc_tests 'jsc'
	jsc_tests 'own'
}

main
