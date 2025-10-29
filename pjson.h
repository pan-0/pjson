
/*
 * MIT No Attribution
 *
 * Copyright 2025 pan <pan_@disroot.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef PJSON_H
#define PJSON_H

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint_least16_t, uint_least32_t */

#ifndef PJSON_API
#	define PJSON_API
#endif

typedef size_t pjson_usize;
typedef uint_least16_t pjson_surrogate;  /* UTF-16 surrogate. */
typedef uint_least32_t pjson_codepoint;  /* Unicode codepoint. */

/*
 * Signifies end of the JSON document to the parser. Can be passed to both
 * `pjson_push()` and `pjson_push_codepoint()`.
 */
enum { PJSON_END = -1 };

enum pjson_status {
	/*
	 * Same as `PJSON_STATUS_ACCEPT`, but the caller must re-push the
	 * byte/codepoint.
	 */
	PJSON_STATUS_ACCEPT_RETRY = 0u,
	/*
	 * The parser has accepted the byte/codepoint and has produced an event
	 * (`enum pjson_event`).
	 */
	PJSON_STATUS_ACCEPT,
	/*
	 * The parser has accepted the byte/codepoint, but no definite event can
	 * be produced yet.
	 */
	PJSON_STATUS_OKAY,
	/*
	 * The parser has encountered an error. See the `PJSON_EVENT_ERROR_*`
	 * events for more.
	 */
	PJSON_STATUS_ERROR,
	/* The parser reached the root rule of JSON's grammar. */
	PJSON_STATUS_DONE
};

enum pjson_event {
	/*
	 * These are valid only if the status is `PJSON_STATUS_ERROR`.
	 */
	PJSON_EVENT_ERROR_UNKNOWN       = -14,  /* Unknown token/character. */
	PJSON_EVENT_ERROR_DIGIT         = -13,  /* Expected (decimal) digit. */
	PJSON_EVENT_ERROR_EXPONENT      = -12,  /* Expected exponent. */
	PJSON_EVENT_ERROR_CODEPOINT     = -11,  /* Invalid codepoint. */
	PJSON_EVENT_ERROR_ESCAPE        = -10,  /* Expected escape character. */
	PJSON_EVENT_ERROR_HEXDIGIT      =  -9,  /* Expected hexadecimal digit. */
	PJSON_EVENT_ERROR_HIGH_LOW      =  -8,  /* Expected high surrogate. */
	PJSON_EVENT_ERROR_LOW           =  -7,  /* Expected low surrogate. */
	PJSON_EVENT_ERROR_TOKEN         =  -6,  /* Unexpected token. */
	PJSON_EVENT_ERROR_EXPECT_STRING =  -5,  /* Expected string. */
	PJSON_EVENT_ERROR_EXPECT_COLON  =  -4,  /* Expected colon (':'). */
	PJSON_EVENT_ERROR_EXPECT_DONE   =  -3,  /* Expected `PJSON_END`. */
	PJSON_EVENT_ERROR_UTF8          =  -2,  /* UTF-8 decoding error. */
	PJSON_EVENT_ERROR_STACK_LIMIT   =  -1,  /* Stack limit reached. */

	/*
	 * This is returned when no event has been produced, but the parser
	 * reported a status (other than `PJSON_STATUS_ACCEPT` and
	 * `PJSON_STATUS_ACCEPT_RETRY`).
	 */
	PJSON_EVENT_NONE                =   0,

	/*
	 * Valid only if the status is `PJSON_STATUS_ACCEPT` or
	 * `PJSON_STATUS_ACCEPT_RETRY`.
	 */
	PJSON_EVENT_NULL                =   1,
	PJSON_EVENT_TRUE                =   2,
	PJSON_EVENT_FALSE               =   3,
	/* Reserved                     =   4, */
	PJSON_EVENT_BEGIN_OBJECT        =   5,
	PJSON_EVENT_BEGIN_ARRAY         =   6,
	PJSON_EVENT_NUMBER_CODE         =   7,
	PJSON_EVENT_END_NUMBER_FLOAT    =   8,
	PJSON_EVENT_END_NUMBER_INTEGER  =   9,
	PJSON_EVENT_END_STRING          =  10,
	PJSON_EVENT_STRING_CODE         =  11,
	PJSON_EVENT_END_OBJECT          =  12,
	/* Reserved                     =  13, */
	PJSON_EVENT_END_ARRAY           =  14
};

enum pjson_state {
	PJSON_STATE_NONE,
	PJSON_STATE_IN_OBJECT,
	PJSON_STATE_IN_KEY,     /* Implies `PJSON_STATE_IN_OBJECT`. */
	PJSON_STATE_IN_ARRAY
};

typedef struct {
	unsigned char status;         /* `enum pjson_status` */
	  signed char event;          /* `enum pjson_event`  */
	unsigned char code_size;      /* UTF-8 encoded bytes size [1, 4]. */
	unsigned char code_bytes[5];  /* UTF-8 encoded bytes, padded with `0`s. */
} pjson_result;

typedef struct {
	unsigned char   utf8_state;   /* UTF-8 decoder state. */
	unsigned char   lexer_state;  /* Lexical analyser state. */
	pjson_surrogate high;         /* UTF-16 high surrogate. */
	pjson_surrogate low;          /* UTF-16 low surrogate. */
	pjson_codepoint utf8_buf;     /* UTF-8 codepoint "buffer". */
	pjson_usize     byte_count;   /* Bytes processed so far. */
	pjson_usize     stack_size;   /* Parser stack size in bytes. */
	pjson_usize     stack_top;    /* Index to the most recently pushed item. */
	unsigned char *restrict stack;
} pjson_context;

typedef struct {
	pjson_usize size;    /* The memory block's size (in bytes). */
	void *restrict ptr;  /* Pointer to the block's first (0th) byte. */
} pjson_block;

PJSON_API void pjson_init(pjson_context *restrict ctx, pjson_block block);

PJSON_API pjson_result pjson_push(pjson_context *ctx, int byte);
PJSON_API pjson_result pjson_push_codepoint(pjson_context *ctx,
                                            pjson_codepoint codepoint);

PJSON_API enum pjson_state pjson_current_state(const pjson_context *ctx);

PJSON_API void pjson_resize(pjson_context *restrict ctx, pjson_block block);
PJSON_API void pjson_reset(pjson_context *ctx);

PJSON_API pjson_usize pjson_position(const pjson_context *ctx);
PJSON_API pjson_block pjson_context_block(const pjson_context *ctx);
PJSON_API pjson_usize pjson_stack_used(const pjson_context *ctx);

#endif  /* PJSON_H */
