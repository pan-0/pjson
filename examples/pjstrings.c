/* Copyright 2025, pan (pan_@disroot.org) */
/* SPDX-License-Identifier: MIT-0 */

/* Like `strings`, but for JSON documents. */

#define _POSIX_C_SOURCE 200809L  /* For `getopt()`. */

#define PJSON_API static
#include "pjson.c"

#include <stddef.h>  /* size_t, NULL */
/*
 * stdin, stderr, stdout, fopen(), fclose(), fread(), fwrite(), ferror(),
 * putchar()
 */
#include <stdio.h>
#include <stdlib.h>  /* EXIT_*, abort() */
#include <string.h>  /* strcmp() */
#include <unistd.h>  /* getopt() */

enum stringtype {
	TYPE_KEYS   = 1u,
	TYPE_VALUES = 1u << 1,
	TYPE_ALL    = TYPE_KEYS | TYPE_VALUES
};

static void print_code(pjson_result res)
{
	fwrite(res.code_bytes, 1, res.code_size, stdout);
}

int main(int argc, char *argv[])
{
	int ret = EXIT_FAILURE;

	enum stringtype type = TYPE_ALL;
	int opt;
	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			if (strcmp(optarg, "keys") == 0)
				type = TYPE_KEYS;
			else if (strcmp(optarg, "values") == 0)
				type = TYPE_VALUES;
			else if (strcmp(optarg, "all") == 0)
				type = TYPE_ALL;
			else
				goto usage;
			break;

		default:
		usage:
			fprintf(stderr, "Usage: %s [-t <keys|values|all>] [FILE]\n",
			        argv[0]);
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

	unsigned char stack_buf[128];
	pjson_context ctx;
	pjson_init(&ctx, (pjson_block){sizeof stack_buf, stack_buf});

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
#				ifdef __GNUC__
				__attribute__((__fallthrough__));
#				endif
			case PJSON_STATUS_ACCEPT_RETRY:
				switch (res.event) {
				case PJSON_EVENT_STRING_CODE:
					if (pjson_current_state(&ctx) == PJSON_STATE_IN_KEY) {
						if (type & TYPE_KEYS)
							print_code(res);
					}
					else if (type & TYPE_VALUES) {
						print_code(res);
					}
					break;
				case PJSON_EVENT_END_STRING:
					if (pjson_current_state(&ctx) == PJSON_STATE_IN_KEY) {
						if (type & TYPE_KEYS)
							putchar('\n');
					}
					else if (type & TYPE_VALUES) {
						putchar('\n');
					}
					break;
				default:
					break;
				}
				break;

			case PJSON_STATUS_OKAY:
				++i;
				break;

			case PJSON_STATUS_ERROR:
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
	if (stream != stdin)
		(void) fclose(stream);

	return ret;
}
