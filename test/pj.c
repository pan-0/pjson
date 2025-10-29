/* Copyright 2025, pan (pan_@disroot.org) */
/* SPDX-License-Identifier: MIT-0 */

#define _POSIX_C_SOURCE 200809L  /* For `getopt()`. */

#define PJSON_API static
#include "pjson.c"

#include <stdbool.h>  /* bool, true, false */
#include <stddef.h>   /* size_t, NULL, SIZE_MAX */
/* stdin, stderr, fread(), fprintf(), ferror(), fopen(), fclose(), FILE */
#include <stdio.h>
/* abort(), strtol(), calloc(), realloc(), free(), EXIT_* */
#include <stdlib.h>
#include <string.h>   /* memset(), strcmp() */
#include <errno.h>    /* errno */
#include <unistd.h>   /* getopt() */

#ifdef __GNUC__
#	define fallthrough __attribute__((__fallthrough__))
#else
#	define fallthrough
#endif

static int errno_helper(int ne)
{
	int e = errno;
	errno = (ne <= 0 ? 0 : ne);
	return e;
}

typedef struct {
	bool err;
	long val;
} atol_res;

static atol_res xatol(const char *str)
{
	int err = errno_helper(0);
	char *endptr;
	long num = strtol(str, &endptr, 10);
	err = errno_helper(err);
	if (err != 0 || endptr == str)
		return (atol_res){.err=true};

	return (atol_res){.err=false, .val=num};
}

static const char *event_name(enum pjson_event ev)
{
	static const char *table[] = {
		[PJSON_EVENT_NULL]               = "Value(Null)",
		[PJSON_EVENT_TRUE]               = "Value(True)",
		[PJSON_EVENT_FALSE]              = "Value(False)",
		[PJSON_EVENT_BEGIN_OBJECT]       = "BeginObject()",
		[PJSON_EVENT_BEGIN_ARRAY]        = "BeginArray()",
		[PJSON_EVENT_NUMBER_CODE]        = "NumberCode",
		[PJSON_EVENT_END_NUMBER_FLOAT]   = "EndFloat()",
		[PJSON_EVENT_END_NUMBER_INTEGER] = "EndInteger()",
		[PJSON_EVENT_END_STRING]         = "EndString()",
		[PJSON_EVENT_STRING_CODE]        = "StringCode",
		[PJSON_EVENT_END_OBJECT]         = "EndObject()",
		[PJSON_EVENT_END_ARRAY]          = "EndArray()"
	};
	return table[ev];
}

int main(int argc, char *argv[])
{
	int ret = EXIT_FAILURE;

	bool disp = false;   /* Display events to `stdout`? (default: no) */
	bool nolim = false;  /* Parser stack unbound? (default: no) */
	pjson_block block = {.size=128};

	int opt;
	while ((opt = getopt(argc, argv, "ds:")) != -1) {
		switch (opt) {
		case 'd':
			disp = true;
			break;
		case 's':
			{
				atol_res res = xatol(optarg);
				if (!res.err && res.val >= 0) {
					if (res.val == 0) {
						nolim = true;
					}
					else {
#						if (LONG_MAX >= SIZE_MAX)
						{
							if (res.val <= (long)SIZE_MAX)
								block.size = (size_t)res.val;
						}
#						else
						{
							if ((size_t)res.val <= SIZE_MAX)
								block.size = (size_t)res.val;
						}
#						endif
					}
					break;
				}
			}
			fallthrough;
		default:  /* '?' */
			fprintf(stderr, "Usage: %s [-s maxsize] [FILE]\n", argv[0]);
			return ret;
		}
	}

	FILE *stream;
	if (optind >= argc || strcmp(argv[optind], "-") == 0) {
		stream = stdin;
	}
	else {
		stream = fopen(argv[optind], "rb");
		if (stream == NULL)
			return ret;
	}

	block.ptr = calloc(block.size, 1);
	if (block.ptr == NULL)
		goto err_calloc;

	pjson_context ctx;
	pjson_init(&ctx, block);

	unsigned char buf[32 * 1024];
	size_t len;
	pjson_result res;

	do {
		len = fread(buf, 1, sizeof buf, stream);
		if (ferror(stream))
			goto end;

		size_t i = 0;
		while (i < len) {
			res = pjson_push(&ctx, buf[i]);
			switch (res.status) {
			case PJSON_STATUS_ACCEPT:
				++i;
				fallthrough;
			case PJSON_STATUS_ACCEPT_RETRY:
				if (disp) {
					printf("%s", event_name(res.event));
					switch (res.event) {
					case PJSON_EVENT_NUMBER_CODE:
					case PJSON_EVENT_STRING_CODE:
						printf("(%s)", res.code_bytes);
						fallthrough;
					default:
						break;
					}
					putchar('\n');
				}
				break;

			case PJSON_STATUS_OKAY:
				++i;
				break;

			case PJSON_STATUS_ERROR:
				if (res.event == PJSON_EVENT_ERROR_STACK_LIMIT && nolim) {
					size_t new_size = block.size << 1;
					if (new_size <= block.size)
						goto end;

					void *new_ptr = realloc(block.ptr, new_size);
					if (new_ptr == NULL)
						goto end;

					memset((char *)new_ptr + block.size,
					       0,
					       new_size - block.size);

					block.size = new_size;
					block.ptr = new_ptr;
					pjson_resize(&ctx, block);
					break;
				}
				goto end;

			case PJSON_STATUS_DONE:
			default:
				abort();
			}
		}
	} while (len == sizeof buf);

retry:
	res = pjson_push(&ctx, PJSON_END);
	switch (res.status) {
	case PJSON_STATUS_ACCEPT_RETRY: goto retry;
	case PJSON_STATUS_DONE:         break;
	default:                        goto end;
	}

	ret = EXIT_SUCCESS;

end:
	free(block.ptr);
err_calloc:
	if (stream != stdin)
		(void) fclose(stream);

	return ret;
}
