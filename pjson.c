
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

#include "pjson.h"
#include <limits.h>   /* CHAR_BIT */
#include <stdbool.h>  /* bool, false, true */

#ifdef __GNUC__
#   define pj_likely(x)   __builtin_expect(!!(x), 1)
#   define pj_unlikely(x) __builtin_expect(!!(x), 0)
#   define pj_fallthrough __attribute__((__fallthrough__))
#   define pj_const       __attribute__((__const__))
#else
#   define pj_likely(x)   (x)
#   define pj_unlikely(x) (x)
#   define pj_fallthrough
#   define pj_const
#endif

/* I love C! */
#ifdef PJ_DEBUG
#   if defined __clang__                          \
            && defined __has_builtin              \
            && __has_builtin(__builtin_debugtrap)
#       define pj_trap_ __builtin_debugtrap
#   elif defined __GNUC__
#       define pj_trap_ __builtin_trap
#   elif defined _MSC_VER
#       define pj_trap_ __debugbreak
#   else
#       include <signal.h>  /* [SIGTRAP | SIGABRT], raise() */
#       ifdef SIGTRAP
#           define pj_trap_() raise(SIGTRAP)
#       else
#           define pj_trap_() raise(SIGABRT)
#       endif
#   endif
#   if PJ_DEBUG == 1
#       include <stdio.h>  /* fprintf(), stderr */
#       if defined __GNUC__
        __attribute__((__noinline__))
#       elif defined _MSC_VER
        __declspec(noinline)
#       endif
        static void pj_assert_print_(const char *file,
                                     int line,
                                     const char *func,
                                     const char *inv)
        {
            fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n",
                    file, line, func, inv);
        }
#   else
#       define pj_assert_print_(...) ((void)0)
#   endif
#   ifdef __GNUC__
#       define pj_assert(c)                                             \
            __extension__ ({                                            \
                if (pj_likely(c)) {                                     \
                    ;                                                   \
                }                                                       \
                else {                                                  \
                    pj_assert_print_(__FILE__, __LINE__, __func__, #c); \
                    pj_trap_();                                         \
                    __asm__ volatile ("nop");  /* GDB is stupid. */     \
                }                                                       \
            })
#   else
#       define pj_assert(c)                                                 \
            ((void)(!(c)                                                    \
                    && (pj_assert_print_(__FILE__, __LINE__, __func__, #c), \
                        pj_trap_(), 0)))
#   endif
#   define pj_assume        pj_assert
#   define pj_unreachable() pj_assert(!"unreachable")
#else
#   define pj_assert(_)
#   if defined __clang__
#       define pj_assume      __builtin_assume
#       define pj_unreachable __builtin_unreachable
#   elif defined __GNUC__
#       if __GNUC__ >= 13
#           define pj_assume(c) __attribute__((__assume__(c)))
#       else
#           define pj_assume(c) \
                __extension__ ({ if (!(c)) __builtin_unreachable(); })
#       endif
#       define pj_unreachable __builtin_unreachable
#   elif defined _MSC_VER
#       define pj_assume        __assume
#       define pj_unreachable() __assume(0)
#   else
#       define pj_assume(_)
#       define pj_unreachable()
#   endif
#endif

/* For GCC's `-Wswitch`. It helps catch unhandled `enum` cases. */
#ifdef PJ_SWITCH
#   define pj_nodefault /* Empty so that warnings are issued. */
#else
#   define pj_nodefault default: pj_unreachable();
#endif

typedef pjson_usize pj_usize;

/* [a, b] */
pj_const inline
static bool pj_inrange(unsigned long x, unsigned long a, unsigned long b)
{
	pj_assume(a < b);

	return x - a < b - a + 1;  /* Depends on wrap-around. */
}


/*
 * Unicode.
 */

enum {
	PJ_SYMBOL_ESC_b         = 0x0008u,
	PJ_SYMBOL_ESC_t         = 0x0009u,
	PJ_SYMBOL_ESC_n         = 0x000Au,
	PJ_SYMBOL_ESC_f         = 0x000Cu,
	PJ_SYMBOL_ESC_r         = 0x000Du,
	PJ_SYMBOL_SPACE         = 0x0020u,
	PJ_SYMBOL_QUOT_MARK     = 0x0022u,
	PJ_SYMBOL_PLUS          = 0x002Bu,
	PJ_SYMBOL_COMMA         = 0x002Cu,
	PJ_SYMBOL_MINUS         = 0x002Du,
	PJ_SYMBOL_DOT           = 0x002Eu,
	PJ_SYMBOL_SLASH         = 0x002Fu,
	PJ_SYMBOL_0             = 0x0030u,
	PJ_SYMBOL_9             = 0x0039u,
	PJ_SYMBOL_COLON         = 0x003Au,
	PJ_SYMBOL_E             = 0x0045u,
	PJ_SYMBOL_LEFT_BRACKET  = 0x005Bu,
	PJ_SYMBOL_BACKSLASH     = 0x005Cu,
	PJ_SYMBOL_RIGHT_BRACKET = 0x005Du,
	PJ_SYMBOL_a             = 0x0061u,
	PJ_SYMBOL_b             = 0x0062u,
	PJ_SYMBOL_e             = 0x0065u,
	PJ_SYMBOL_f             = 0x0066u,
	PJ_SYMBOL_l             = 0x006Cu,
	PJ_SYMBOL_n             = 0x006Eu,
	PJ_SYMBOL_r             = 0x0072u,
	PJ_SYMBOL_s             = 0x0073u,
	PJ_SYMBOL_t             = 0x0074u,
	PJ_SYMBOL_u             = 0x0075u,
	PJ_SYMBOL_LEFT_CURLY    = 0x007Bu,
	PJ_SYMBOL_RIGHT_CURLY   = 0x007Du
};

enum pj_utf8_status {
	PJ_UTF8_ACCEPT = PJSON_STATUS_ACCEPT,
	PJ_UTF8_OKAY   = PJSON_STATUS_OKAY,
	PJ_UTF8_ERROR  = PJSON_STATUS_ERROR
};

typedef struct {
	enum pj_utf8_status status;
	pjson_codepoint codepoint;
} pj_utf8_result;

enum pj_utf8_state {
	PJ_UTF8_STATE_BOM_0,
	PJ_UTF8_STATE_BOM_1,
	PJ_UTF8_STATE_BOM_2,
	PJ_UTF8_STATE_0,
	PJ_UTF8_STATE_1,
	PJ_UTF8_STATE_2,
	PJ_UTF8_STATE_2_1,
	PJ_UTF8_STATE_3,
	PJ_UTF8_STATE_3_1,
	PJ_UTF8_STATE_3_2,
};

static pj_utf8_result pj_utf8_push(pjson_context *ctx, unsigned byte)
{
	pj_assume((byte <= 0xFF) | (byte == (unsigned)PJSON_END));

	switch ((enum pj_utf8_state)ctx->utf8_state) {
	case PJ_UTF8_STATE_BOM_0:
		if (pj_unlikely((byte & 0xFF) == 0xEF)) {
			ctx->utf8_state = PJ_UTF8_STATE_BOM_1;
			return (pj_utf8_result){PJ_UTF8_OKAY};
		}
		ctx->utf8_state = PJ_UTF8_STATE_0;
		goto state_0;
	case PJ_UTF8_STATE_BOM_1:
		if (pj_unlikely((byte & 0xFF) != 0xBB))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		ctx->utf8_state = PJ_UTF8_STATE_BOM_2;
		return (pj_utf8_result){PJ_UTF8_OKAY};
	case PJ_UTF8_STATE_BOM_2:
		if (pj_unlikely((byte & 0xFF) != 0xBF))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		ctx->utf8_state = PJ_UTF8_STATE_0;
		return (pj_utf8_result){PJ_UTF8_OKAY};

	case PJ_UTF8_STATE_0:
	state_0:
		if (pj_unlikely((byte & 0x80) != 0)) {
			if ((byte & 0xE0) == 0xC0)
				ctx->utf8_state = PJ_UTF8_STATE_1;
			else if ((byte & 0xF0) == 0xE0)
				ctx->utf8_state = PJ_UTF8_STATE_2;
			else if ((byte & 0xF8) == 0xF0)
				ctx->utf8_state = PJ_UTF8_STATE_3;
			else if (byte == (unsigned)PJSON_END)  /* <- Beautiful hack. */
				return (pj_utf8_result){PJ_UTF8_ACCEPT,
				                        (pjson_codepoint)PJSON_END};
			else
				return (pj_utf8_result){PJ_UTF8_ERROR};

			ctx->utf8_buf = byte;
			return (pj_utf8_result){PJ_UTF8_OKAY};
		}
		return (pj_utf8_result){PJ_UTF8_ACCEPT, (pjson_codepoint)byte};

	case PJ_UTF8_STATE_1:
		if (pj_unlikely((byte & 0xC0) != 0x80))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		{
			pjson_codepoint buf = ctx->utf8_buf;
			buf |= (pjson_codepoint)(byte & 0xFF) << 8;
			pjson_codepoint res = (buf & 0x1F) << 6 | ((buf >> 8) & 0x3F);
			if (pj_unlikely(res < 128))
				return (pj_utf8_result){PJ_UTF8_ERROR};

			ctx->utf8_state = PJ_UTF8_STATE_0;
			return (pj_utf8_result){PJ_UTF8_ACCEPT, res};
		}

	case PJ_UTF8_STATE_2:
		if (pj_unlikely((byte & 0xC0) != 0x80))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		ctx->utf8_buf |= (pjson_codepoint)(byte & 0xFF) << 8;
		ctx->utf8_state = PJ_UTF8_STATE_2_1;
		return (pj_utf8_result){PJ_UTF8_OKAY};
	case PJ_UTF8_STATE_2_1:
		if (pj_unlikely((byte & 0xC0) != 0x80))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		{
			pjson_codepoint buf = ctx->utf8_buf;
			buf |= (pjson_codepoint)(byte & 0xFF) << 16;
			buf &= 0x3F3F0F;
			pjson_codepoint res = (buf & 0xFF) << 12
			                      | (buf & 0xFF00) >> 2
			                      | (buf & 16);
			if (pj_unlikely(!(pj_inrange(res, 2048, 55295) | (res > 57343))))
				return (pj_utf8_result){PJ_UTF8_ERROR};

			ctx->utf8_state = PJ_UTF8_STATE_0;
			return (pj_utf8_result){PJ_UTF8_ACCEPT, res};
		}

	case PJ_UTF8_STATE_3:
		if (pj_unlikely((byte & 0xC0) != 0x80))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		ctx->utf8_buf |= (pjson_codepoint)(byte & 0xFF) << 8;
		ctx->utf8_state = PJ_UTF8_STATE_3_1;
		return (pj_utf8_result){PJ_UTF8_OKAY};
	case PJ_UTF8_STATE_3_1:
		if (pj_unlikely((byte & 0xC0) != 0x80))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		ctx->utf8_buf |= (pjson_codepoint)(byte & 0xFF) << 16;
		ctx->utf8_state = PJ_UTF8_STATE_3_2;
		return (pj_utf8_result){PJ_UTF8_OKAY};
	case PJ_UTF8_STATE_3_2:
		if (pj_unlikely((byte & 0xC0) != 0x80))
			return (pj_utf8_result){PJ_UTF8_ERROR};

		{
			pjson_codepoint buf = ctx->utf8_buf;
			buf |= (pjson_codepoint)(byte & 0xFF) << 24;
			buf &= 0x3F3F3F07;
			pjson_codepoint res = (buf & 0xFF) << 18
			                      | (buf & 0xFF00) << 4
			                      | (buf & 0xFF0000) >> 10
			                      | buf >> 24;
			if (pj_unlikely(!pj_inrange(res, 65536, 1114111)))
				return (pj_utf8_result){PJ_UTF8_ERROR};

			ctx->utf8_state = PJ_UTF8_STATE_0;
			return (pj_utf8_result){PJ_UTF8_ACCEPT, res};
		}

	pj_nodefault
	}

	pj_unreachable();
}

/* Assumes that `codepoint` is valid. */
pj_const inline static pj_usize pj_utf8_code_size(pjson_codepoint codepoint)
{
	return 1 + (codepoint >= 0x0080)
	         + (codepoint >= 0x0800)
	         + (codepoint >= 0x10000);
}

/* <https://unicodebook.readthedocs.io/unicode_encodings.html> */

pj_const inline static bool pj_is_surrogate(pjson_codepoint codepoint)
{
	return pj_inrange(codepoint, 0xD800, 0xDFFF);
}

pj_const inline static bool pj_is_high_surrogate(pjson_codepoint codepoint)
{
	return pj_inrange(codepoint, 0xD800, 0xDBFF);
}

pj_const inline static bool pj_is_low_surrogate(pjson_surrogate codepoint)
{
	return pj_inrange(codepoint, 0xDC00, 0xDFFF);
}

pj_const inline static bool pj_is_valid_codepoint(pjson_codepoint codepoint)
{
	/*
	 * Allow non-characters.
	 *
	 * <https://www.unicode.org/versions/corrigendum9.html>
	 */
	return pj_inrange(codepoint, 0, 0x10FFFF) && !pj_is_surrogate(codepoint);
}

static pj_usize pj_utf8_encode(unsigned char *dest, pjson_codepoint codepoint)
{
	pj_assume(pj_is_valid_codepoint(codepoint));

	if (pj_likely(codepoint < 0x0080)) {
		dest[0] = codepoint & 0xFF;
		return 1;
	}

	if (codepoint < 0x0800) {
		dest[0] = 0xC0 | (codepoint >> 6);
		dest[1] = 0x80 | (codepoint & 0x3F);
		return 2;
	}

	if (codepoint < 0x10000) {
		dest[0] = 0xE0 | (codepoint >> 12);
		dest[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		dest[2] = 0x80 | (codepoint & 0x3F);
		return 3;
	}

	dest[0] = 0xF0 | (codepoint >> 18);
	dest[1] = 0x80 | ((codepoint >> 12) & 0x3F);
	dest[2] = 0x80 | ((codepoint >> 6) & 0x3F);
	dest[3] = 0x80 | (codepoint & 0x3F);
	return 4;
}

pj_const inline static
pjson_codepoint pj_utf16_decode_pair(pjson_surrogate high, pjson_surrogate low)
{
	pj_assume(pj_is_high_surrogate(high) & pj_is_low_surrogate(low));

	unsigned long res = 0x10000ul + ((high & 0x03FFul) << 10) + (low & 0x03FF);
	return res & (pjson_codepoint)-1;
}


/*
 * Lexer.
 */

pj_const inline static bool pj_is_digit(pjson_codepoint codepoint)
{
	return pj_inrange(codepoint, PJ_SYMBOL_0, PJ_SYMBOL_9);
}

pj_const inline static bool pj_is_hexdigit(pjson_codepoint codepoint)
{
	return pj_is_digit(codepoint)
	       | (((unsigned long)codepoint | 32) - PJ_SYMBOL_a < 6);
}

pj_const inline static unsigned pj_hex_to_bin(pjson_codepoint codepoint)
{
	pj_assume(pj_is_hexdigit(codepoint));

	return ((codepoint & 0xF) + (codepoint >> 6)) | ((codepoint >> 3) & 0x8);
}

enum pj_lexer_state {
	PJ_LEXER_STATE_INITIAL,

	PJ_LEXER_STATE_STRING,
	PJ_LEXER_STATE_STRING_ESC,
	PJ_LEXER_STATE_STRING_HIGH_u0,
	PJ_LEXER_STATE_STRING_HIGH_u1,
	PJ_LEXER_STATE_STRING_HIGH_u2,
	PJ_LEXER_STATE_STRING_HIGH_u3,

	PJ_LEXER_STATE_STRING_LOW_ESC,
	PJ_LEXER_STATE_STRING_LOW_u,
	PJ_LEXER_STATE_STRING_LOW_u0,
	PJ_LEXER_STATE_STRING_LOW_u1,
	PJ_LEXER_STATE_STRING_LOW_u2,
	PJ_LEXER_STATE_STRING_LOW_u3,

	PJ_LEXER_STATE_NUMBER_019,
	PJ_LEXER_STATE_NUMBER_09,
	PJ_LEXER_STATE_NUMBER_DOT_EXP,
	PJ_LEXER_STATE_NUMBER_FRAC_09,
	PJ_LEXER_STATE_NUMBER_FRAC_09b,
	PJ_LEXER_STATE_NUMBER_EXP_SIGN,
	PJ_LEXER_STATE_NUMBER_EXP_09,
	PJ_LEXER_STATE_NUMBER_EXP_09b,

	PJ_LEXER_STATE_NULL_u,
	PJ_LEXER_STATE_NULL_l,
	PJ_LEXER_STATE_NULL_l2,

	PJ_LEXER_STATE_TRUE_r,
	PJ_LEXER_STATE_TRUE_u,
	PJ_LEXER_STATE_TRUE_e,

	PJ_LEXER_STATE_FALSE_a,
	PJ_LEXER_STATE_FALSE_l,
	PJ_LEXER_STATE_FALSE_s,
	PJ_LEXER_STATE_FALSE_e
};

enum pj_lexer_event {
	PJ_LEXER_EVENT_NONE                = PJSON_EVENT_NONE,
	PJ_LEXER_EVENT_TOKEN_NULL          = PJSON_EVENT_NULL,
	PJ_LEXER_EVENT_TOKEN_TRUE          = PJSON_EVENT_TRUE,
	PJ_LEXER_EVENT_TOKEN_FALSE         = PJSON_EVENT_FALSE,
	PJ_LEXER_EVENT_BEGIN_STRING,
	PJ_LEXER_EVENT_TOKEN_LEFT_CURLY    = PJSON_EVENT_BEGIN_OBJECT,
	PJ_LEXER_EVENT_TOKEN_LEFT_BRACKET  = PJSON_EVENT_BEGIN_ARRAY,
	PJ_LEXER_EVENT_NUMBER_CODE         = PJSON_EVENT_NUMBER_CODE,
	PJ_LEXER_EVENT_END_NUMBER_FLOAT    = PJSON_EVENT_END_NUMBER_FLOAT,
	PJ_LEXER_EVENT_END_NUMBER_INTEGER  = PJSON_EVENT_END_NUMBER_INTEGER,
	PJ_LEXER_EVENT_END_STRING          = PJSON_EVENT_END_STRING,
	PJ_LEXER_EVENT_STRING_CODE         = PJSON_EVENT_STRING_CODE,
	PJ_LEXER_EVENT_TOKEN_RIGHT_CURLY   = PJSON_EVENT_END_OBJECT,
	PJ_LEXER_EVENT_TOKEN_COMMA,
	PJ_LEXER_EVENT_TOKEN_RIGHT_BRACKET = PJSON_EVENT_END_ARRAY,
	PJ_LEXER_EVENT_TOKEN_COLON,
	PJ_LEXER_EVENT_DONE
};

pj_const
inline static pjson_result pj_lexer_coded_result(enum pjson_status status,
                                                 enum pj_lexer_event event,
                                                 pjson_codepoint codepoint)
{
	/*
	 * Subtle: This also zero-initializes the `code_bytes` array, which we
	 *         want.
	 */
	pjson_result res = {status, (enum pjson_event)event};

	pj_usize len = pj_utf8_encode(res.code_bytes, codepoint);
	pj_assume(len <= 4);
	res.code_size = (unsigned char)len;

	return res;
}

pj_const
inline static pjson_result pj_lexer_ascii_result(enum pjson_status status,
                                                 enum pj_lexer_event event,
                                                 pjson_codepoint codepoint)
{
	pj_assume(codepoint < 128);

	return (pjson_result){.status    =status,
	                      .event     =(enum pjson_event)event,
	                      .code_size =1,
	                      .code_bytes={codepoint, '\0'}};
}

static
pjson_result pj_lexer_push(pjson_context *ctx, pjson_codepoint codepoint)
{
	pj_assume(pj_is_valid_codepoint(codepoint)
	          | (codepoint == (pjson_codepoint)PJSON_END));

	switch ((enum pj_lexer_state)ctx->lexer_state) {
	case PJ_LEXER_STATE_INITIAL:
		switch (codepoint) {
		case (pjson_codepoint)PJSON_END:
			return (pjson_result){PJSON_STATUS_ACCEPT, PJ_LEXER_EVENT_DONE};

		case PJ_SYMBOL_ESC_t:
		case PJ_SYMBOL_ESC_n:
		case PJ_SYMBOL_ESC_r:
		case PJ_SYMBOL_SPACE:
			return (pjson_result){PJSON_STATUS_OKAY};

		case PJ_SYMBOL_COMMA:
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_TOKEN_COMMA};
		case PJ_SYMBOL_COLON:
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_TOKEN_COLON};
		case PJ_SYMBOL_LEFT_BRACKET:
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_TOKEN_LEFT_BRACKET};
		case PJ_SYMBOL_RIGHT_BRACKET:
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_TOKEN_RIGHT_BRACKET};
		case PJ_SYMBOL_LEFT_CURLY:
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_TOKEN_LEFT_CURLY};
		case PJ_SYMBOL_RIGHT_CURLY:
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_TOKEN_RIGHT_CURLY};

		case PJ_SYMBOL_n:
			ctx->lexer_state = PJ_LEXER_STATE_NULL_u;
			return (pjson_result){PJSON_STATUS_OKAY};

		case PJ_SYMBOL_t:
			ctx->lexer_state = PJ_LEXER_STATE_TRUE_r;
			return (pjson_result){PJSON_STATUS_OKAY};

		case PJ_SYMBOL_f:
			ctx->lexer_state = PJ_LEXER_STATE_FALSE_a;
			return (pjson_result){PJSON_STATUS_OKAY};

		case PJ_SYMBOL_MINUS:
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_019;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             PJ_SYMBOL_MINUS);

		case PJ_SYMBOL_0:
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_DOT_EXP;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             PJ_SYMBOL_0);

		case PJ_SYMBOL_QUOT_MARK:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_BEGIN_STRING};

		default:
			if (pj_is_digit(codepoint)) {
				pj_assert(codepoint != PJ_SYMBOL_0);
				ctx->lexer_state = PJ_LEXER_STATE_NUMBER_09;
				return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
				                             PJ_LEXER_EVENT_NUMBER_CODE,
				                             codepoint);
			}

			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};
		}

	/* [n]ull */
	case PJ_LEXER_STATE_NULL_u:
		if (pj_unlikely(codepoint != PJ_SYMBOL_u))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_NULL_l;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_NULL_l:
		if (pj_unlikely(codepoint != PJ_SYMBOL_l))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_NULL_l2;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_NULL_l2:
		if (pj_unlikely(codepoint != PJ_SYMBOL_l))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
		return (pjson_result){PJSON_STATUS_ACCEPT, PJ_LEXER_EVENT_TOKEN_NULL};

	/* [t]rue */
	case PJ_LEXER_STATE_TRUE_r:
		if (pj_unlikely(codepoint != PJ_SYMBOL_r))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_TRUE_u;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_TRUE_u:
		if (pj_unlikely(codepoint != PJ_SYMBOL_u))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_TRUE_e;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_TRUE_e:
		if (pj_unlikely(codepoint != PJ_SYMBOL_e))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
		return (pjson_result){PJSON_STATUS_ACCEPT, PJ_LEXER_EVENT_TOKEN_TRUE};

	/* [f]alse */
	case PJ_LEXER_STATE_FALSE_a:
		if (pj_unlikely(codepoint != PJ_SYMBOL_a))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_FALSE_l;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_FALSE_l:
		if (pj_unlikely(codepoint != PJ_SYMBOL_l))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_FALSE_s;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_FALSE_s:
		if (pj_unlikely(codepoint != PJ_SYMBOL_s))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_FALSE_e;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_FALSE_e:
		if (pj_unlikely(codepoint != PJ_SYMBOL_e))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_UNKNOWN};

		ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
		return (pjson_result){PJSON_STATUS_ACCEPT, PJ_LEXER_EVENT_TOKEN_FALSE};

	/*
	 * number = '-'? int frac? exp?
	 * int    = '0' / ( [1-9] [0-9]* )
	 * frac   = '.' [0-9]+
	 * exp    = [eE] [\-\+]? [0-9]+
	 */
	case PJ_LEXER_STATE_NUMBER_019:
		if (pj_unlikely(!pj_is_digit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_DIGIT};

		if (codepoint == PJ_SYMBOL_0) {
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_DOT_EXP;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             PJ_SYMBOL_0);
		}

		ctx->lexer_state = PJ_LEXER_STATE_NUMBER_09;
		return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
		                             PJ_LEXER_EVENT_NUMBER_CODE,
		                             codepoint);
	case PJ_LEXER_STATE_NUMBER_09:
		switch (codepoint) {
		case PJ_SYMBOL_DOT:
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_FRAC_09;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             PJ_SYMBOL_DOT);
		case PJ_SYMBOL_E:
		case PJ_SYMBOL_e:
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_EXP_SIGN;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             codepoint);
		default:
			if (pj_is_digit(codepoint))
				return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
				                             PJ_LEXER_EVENT_NUMBER_CODE,
				                             codepoint);

			ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
			return (pjson_result){PJSON_STATUS_ACCEPT_RETRY,
			                      PJ_LEXER_EVENT_END_NUMBER_INTEGER};
		}
	case PJ_LEXER_STATE_NUMBER_DOT_EXP:
		switch (codepoint) {
		case PJ_SYMBOL_DOT:
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_FRAC_09;
			break;
		case PJ_SYMBOL_E:
		case PJ_SYMBOL_e:
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_EXP_SIGN;
			break;
		default:
			ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
			return (pjson_result){PJSON_STATUS_ACCEPT_RETRY,
			                      PJ_LEXER_EVENT_END_NUMBER_INTEGER};
		}
		return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
		                             PJ_LEXER_EVENT_NUMBER_CODE,
		                             codepoint);
	case PJ_LEXER_STATE_NUMBER_FRAC_09:
		if (pj_unlikely(!pj_is_digit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_DIGIT};

		ctx->lexer_state = PJ_LEXER_STATE_NUMBER_FRAC_09b;
		return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
		                             PJ_LEXER_EVENT_NUMBER_CODE,
		                             codepoint);
	case PJ_LEXER_STATE_NUMBER_FRAC_09b:
		if (pj_is_digit(codepoint))
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             codepoint);

		if ((codepoint == PJ_SYMBOL_E) | (codepoint == PJ_SYMBOL_e)) {
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_EXP_SIGN;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             codepoint);
		}

		ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
		return (pjson_result){PJSON_STATUS_ACCEPT_RETRY,
		                      PJ_LEXER_EVENT_END_NUMBER_FLOAT};
	case PJ_LEXER_STATE_NUMBER_EXP_SIGN:
		if ((codepoint == PJ_SYMBOL_MINUS) | (codepoint == PJ_SYMBOL_PLUS)) {
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_EXP_09;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             codepoint);
		}

		if (pj_is_digit(codepoint)) {
			ctx->lexer_state = PJ_LEXER_STATE_NUMBER_EXP_09b;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             codepoint);
		}

		return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_EXPONENT};
	case PJ_LEXER_STATE_NUMBER_EXP_09:
		if (pj_unlikely(!pj_is_digit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_EXPONENT};

		ctx->lexer_state = PJ_LEXER_STATE_NUMBER_EXP_09b;
		return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
		                             PJ_LEXER_EVENT_NUMBER_CODE,
		                             codepoint);
	case PJ_LEXER_STATE_NUMBER_EXP_09b:
		if (pj_is_digit(codepoint))
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_NUMBER_CODE,
			                             codepoint);

		ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
		return (pjson_result){PJSON_STATUS_ACCEPT_RETRY,
		                      PJ_LEXER_EVENT_END_NUMBER_FLOAT};

	/*
	 * string = quotation-mark *char quotation-mark
	 * char = unescaped /
	 *        escape (
	 *        %x22 /          ; "    quotation mark  U+0022
	 *        %x5C /          ; \    reverse solidus U+005C
	 *        %x2F /          ; /    solidus         U+002F
	 *        %x62 /          ; b    backspace       U+0008
	 *        %x66 /          ; f    form feed       U+000C
	 *        %x6E /          ; n    line feed       U+000A
	 *        %x72 /          ; r    carriage return U+000D
	 *        %x74 /          ; t    tab             U+0009
	 *        %x75 4HEXDIG )  ; uXXXX                U+XXXX
	 * escape = %x5C          ; \
	 * quotation-mark = %x22  ; "
	 * unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
	 */
	case PJ_LEXER_STATE_STRING:
		if (pj_unlikely(codepoint == PJ_SYMBOL_QUOT_MARK)) {
			ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
			return (pjson_result){PJSON_STATUS_ACCEPT,
			                      PJ_LEXER_EVENT_END_STRING};
		}

		if (pj_unlikely(codepoint == PJ_SYMBOL_BACKSLASH)) {
			ctx->lexer_state = PJ_LEXER_STATE_STRING_ESC;
			return (pjson_result){PJSON_STATUS_OKAY};
		}

		/*
		 * Allowed unescaped characters per RFC 8259. This check also takes
		 * care of (disallowed) unescaped control characters.
		 */
		if (pj_unlikely(!(pj_inrange(codepoint, 0x20, 0x21)
		                  | pj_inrange(codepoint, 0x23, 0x5B)
		                  | pj_inrange(codepoint, 0x5D, 0x10FFFF))))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_CODEPOINT};

		return pj_lexer_coded_result(PJSON_STATUS_ACCEPT,
		                             PJ_LEXER_EVENT_STRING_CODE,
		                             codepoint);
	case PJ_LEXER_STATE_STRING_ESC:
		switch (codepoint) {
		case PJ_SYMBOL_QUOT_MARK:
		case PJ_SYMBOL_BACKSLASH:
		case PJ_SYMBOL_SLASH:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             codepoint);
		case PJ_SYMBOL_b:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             PJ_SYMBOL_ESC_b);
		case PJ_SYMBOL_f:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             PJ_SYMBOL_ESC_f);
		case PJ_SYMBOL_n:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             PJ_SYMBOL_ESC_n);
		case PJ_SYMBOL_r:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             PJ_SYMBOL_ESC_r);
		case PJ_SYMBOL_t:
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_ascii_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             PJ_SYMBOL_ESC_t);

		case PJ_SYMBOL_u:
			ctx->lexer_state = PJ_LEXER_STATE_STRING_HIGH_u0;
			return (pjson_result){PJSON_STATUS_OKAY};

		default:
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_ESCAPE};
		}
	case PJ_LEXER_STATE_STRING_HIGH_u0:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		ctx->high = pj_hex_to_bin(codepoint) << 12;
		ctx->lexer_state = PJ_LEXER_STATE_STRING_HIGH_u1;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_HIGH_u1:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		ctx->high |= pj_hex_to_bin(codepoint) << 8;
		ctx->lexer_state = PJ_LEXER_STATE_STRING_HIGH_u2;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_HIGH_u2:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		ctx->high |= pj_hex_to_bin(codepoint) << 4;
		ctx->lexer_state = PJ_LEXER_STATE_STRING_HIGH_u3;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_HIGH_u3:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		{
			pjson_surrogate high = ctx->high | pj_hex_to_bin(codepoint);
			if (pj_unlikely(pj_is_surrogate(high))) {
				/*
				 * DON'T use a replacement character! Intentionally be as
				 * strict as possible.
				 */
				if (pj_unlikely(pj_is_low_surrogate(high)))
					return (pjson_result){PJSON_STATUS_ERROR,
					                      PJSON_EVENT_ERROR_HIGH_LOW};

				pj_assert(pj_is_high_surrogate(high));
				ctx->high = high;
				ctx->lexer_state = PJ_LEXER_STATE_STRING_LOW_ESC;
				return (pjson_result){PJSON_STATUS_OKAY};
			}

			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_coded_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             (pjson_codepoint)high);
		}
	case PJ_LEXER_STATE_STRING_LOW_ESC:
		if (pj_unlikely(codepoint != PJ_SYMBOL_BACKSLASH))
			return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_LOW};

		ctx->lexer_state = PJ_LEXER_STATE_STRING_LOW_u;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_LOW_u:
		if (pj_unlikely(codepoint != PJ_SYMBOL_u))
			return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_LOW};

		ctx->lexer_state = PJ_LEXER_STATE_STRING_LOW_u0;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_LOW_u0:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		ctx->low = pj_hex_to_bin(codepoint) << 12;
		ctx->lexer_state = PJ_LEXER_STATE_STRING_LOW_u1;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_LOW_u1:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		ctx->low |= pj_hex_to_bin(codepoint) << 8;
		ctx->lexer_state = PJ_LEXER_STATE_STRING_LOW_u2;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_LOW_u2:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		ctx->low |= pj_hex_to_bin(codepoint) << 4;
		ctx->lexer_state = PJ_LEXER_STATE_STRING_LOW_u3;
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_LEXER_STATE_STRING_LOW_u3:
		if (pj_unlikely(!pj_is_hexdigit(codepoint)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_HEXDIGIT};

		{
			pjson_surrogate low = ctx->low | pj_hex_to_bin(codepoint);
			if (pj_unlikely(!pj_is_low_surrogate(low)))
				return (pjson_result){PJSON_STATUS_ERROR,
				                      PJSON_EVENT_ERROR_LOW};

			codepoint = pj_utf16_decode_pair(ctx->high, low);
			ctx->lexer_state = PJ_LEXER_STATE_STRING;
			return pj_lexer_coded_result(PJSON_STATUS_ACCEPT,
			                             PJ_LEXER_EVENT_STRING_CODE,
			                             codepoint);
		}

	pj_nodefault
	}

	pj_unreachable();
}


/*
 * Parser.
 */

enum pj_parser_state {
	PJ_PARSER_STATE_VALUE         = 0u,
	PJ_PARSER_STATE_WAIT_TOKEN    = 1u,

	PJ_PARSER_STATE_ELEMENT_LIST  = 2u,
	PJ_PARSER_STATE_ELEMENT_AFTER = 3u,

	PJ_PARSER_STATE_MEMBER_LIST   = 4u,
	PJ_PARSER_STATE_MEMBER_STRING = 5u,
	PJ_PARSER_STATE_MEMBER_COLON  = 6u,
	PJ_PARSER_STATE_MEMBER_AFTER  = 7u,

	PJ_PARSER_STATE_DONE          = 8u
};

/*
 * Fallback config option in case the compiler can't be trusted with optimising
 * the divisions/remainders to shifts/masks.
 */
#ifndef PJ_NODIV

#define PJ_MIN_STACK 1  /* Mininum stack size in bytes. */

enum {
	PJ_STATE_BITS = 4u,
	PJ_COLUMNS    = (unsigned)CHAR_BIT / PJ_STATE_BITS,
	PJ_MASK       = (1u << PJ_STATE_BITS) - 1
};

inline static
enum pj_parser_state pj_stack_get(const pjson_context *ctx, pj_usize index)
{
	/*
	 *   7654 3210
	 *      1    0
	 * 0 SSSS SSSS  1 0
	 * 1 SSSS SSSS  3 2
	 *      ...
	 */
	pj_usize row = index / PJ_COLUMNS;
	pj_usize col = index % PJ_COLUMNS;
	return (enum pj_parser_state)((ctx->stack[row] >> (col * PJ_STATE_BITS))
	                              & PJ_MASK);
}

inline static
void pj_stack_set(pjson_context *ctx,
                  pj_usize index,
                  enum pj_parser_state state)
{
	pj_usize row = index / PJ_COLUMNS;
	pj_usize col = index % PJ_COLUMNS;
	unsigned char *restrict stack = ctx->stack;
	stack[row] &= ~(PJ_MASK << (col * PJ_STATE_BITS));
	stack[row] |= state << (col * PJ_STATE_BITS);
}

/* x != 0 */
#define pj_ceil_div(x, y) (1 + ((x) - 1) / (y))

inline static bool pj_stack_is_topped(const pjson_context *ctx, pj_usize top)
{
	return (top == 0) | (pj_ceil_div(top, PJ_COLUMNS) >= ctx->stack_size);
}

inline static pj_usize pj_stack_used(const pjson_context *ctx)
{
	return pj_ceil_div(ctx->stack_top + 1, PJ_COLUMNS);
}

#else

#define PJ_MIN_STACK 2
#define pj_stack_get(ctx, index) ((enum pj_parser_state)(ctx)->stack[(index)])
#define pj_stack_set(ctx, index, state) ((ctx)->stack[(index)] = (state))

inline static bool pj_stack_is_topped(const pjson_context *ctx, pj_usize top)
{
	return (top == 0) | (top >= ctx->stack_size);
}

inline static pj_usize pj_stack_used(const pjson_context *ctx)
{
	return ctx->stack_top + 1;  /* Hopefully no wrap-around. */
}

#endif  /* PJ_NODIV */

inline static bool pj_stack_reserve_top(pjson_context *ctx)
{
	pj_usize top = ctx->stack_top + 1;
	if (pj_unlikely(pj_stack_is_topped(ctx, top)))
		return true;

	ctx->stack_top = top;
	return false;
}

inline
static bool pj_stack_push(pjson_context *ctx, enum pj_parser_state state)
{
	pj_usize top = ctx->stack_top + 1;
	if (pj_unlikely(pj_stack_is_topped(ctx, top)))
		return true;

	pj_stack_set(ctx, top, state);
	ctx->stack_top = top;
	return false;
}

inline static void pj_stack_pop(pjson_context *ctx)
{
	pj_assume(ctx->stack_top > 0);

	--(ctx->stack_top);
}

static pjson_result pj_parse_value(pjson_context *ctx,
                                   pjson_result lexer_res,
                                   pj_usize top)
{
	switch (lexer_res.event) {
	case PJ_LEXER_EVENT_TOKEN_NULL:
	case PJ_LEXER_EVENT_TOKEN_TRUE:
	case PJ_LEXER_EVENT_TOKEN_FALSE:
		pj_stack_pop(ctx);
		return lexer_res;

	case PJ_LEXER_EVENT_BEGIN_STRING:
		pj_stack_set(ctx, top, PJ_PARSER_STATE_WAIT_TOKEN);
		return (pjson_result){PJSON_STATUS_OKAY};

	case PJ_LEXER_EVENT_TOKEN_LEFT_CURLY:
		pj_stack_set(ctx, top, PJ_PARSER_STATE_MEMBER_LIST);
		return lexer_res;

	case PJ_LEXER_EVENT_TOKEN_LEFT_BRACKET:
		pj_stack_set(ctx, top, PJ_PARSER_STATE_ELEMENT_LIST);
		return lexer_res;

	case PJ_LEXER_EVENT_NUMBER_CODE:
		pj_stack_set(ctx, top, PJ_PARSER_STATE_WAIT_TOKEN);
		return lexer_res;

	default:
		return (pjson_result){PJSON_STATUS_ERROR,
		                      PJSON_EVENT_ERROR_EXPECT_VALUE};
	}
}

static pjson_result pj_parser_push(pjson_context *ctx, pjson_result lexer_res)
{
	/*
	 * root = value
	 *
	 * value = null | true | false | number | string | object | array
	 *
	 * object = {}
	 *        | { member-list }
	 *
	 * member-list = member
	 *             | member , member-list
	 * member = string : value
	 *
	 * array = []
	 *       | [ element-list ]
	 * element-list = value
	 *              | value , element-list
	 */

	pj_usize top = ctx->stack_top;
	switch (pj_stack_get(ctx, top)) {
	/* value */
	case PJ_PARSER_STATE_VALUE:
		return pj_parse_value(ctx, lexer_res, top);
	case PJ_PARSER_STATE_WAIT_TOKEN:
		switch (lexer_res.event) {
		case PJ_LEXER_EVENT_END_NUMBER_FLOAT:
		case PJ_LEXER_EVENT_END_NUMBER_INTEGER:
		case PJ_LEXER_EVENT_END_STRING:
			pj_stack_pop(ctx);
			pj_fallthrough;
		default:
			return lexer_res;
		}

	/* array */
	case PJ_PARSER_STATE_ELEMENT_LIST:
		if (lexer_res.event == PJ_LEXER_EVENT_TOKEN_RIGHT_BRACKET) {
			pj_stack_pop(ctx);
			return lexer_res;
		}

		if (pj_unlikely(pj_stack_reserve_top(ctx)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_STACK_LIMIT};

		lexer_res = pj_parse_value(ctx, lexer_res, top + 1);
		if (pj_unlikely(lexer_res.status == PJSON_STATUS_ERROR))
			pj_stack_pop(ctx);
		else
			pj_stack_set(ctx, top, PJ_PARSER_STATE_ELEMENT_AFTER);
		return lexer_res;
	case PJ_PARSER_STATE_ELEMENT_AFTER:
		switch (lexer_res.event) {
		case PJ_LEXER_EVENT_TOKEN_COMMA:
			if (pj_unlikely(pj_stack_push(ctx, PJ_PARSER_STATE_VALUE)))
				return (pjson_result){PJSON_STATUS_ERROR,
				                      PJSON_EVENT_ERROR_STACK_LIMIT};

			pj_stack_set(ctx, top, PJ_PARSER_STATE_ELEMENT_AFTER);
			return (pjson_result){PJSON_STATUS_OKAY};
		case PJ_LEXER_EVENT_TOKEN_RIGHT_BRACKET:
			pj_stack_pop(ctx);
			return lexer_res;
		default:
			return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_TOKEN};
		}

	/* object */
	case PJ_PARSER_STATE_MEMBER_LIST:
		switch (lexer_res.event) {
		case PJ_LEXER_EVENT_BEGIN_STRING:
			if (pj_unlikely(pj_stack_push(ctx, PJ_PARSER_STATE_WAIT_TOKEN)))
				return (pjson_result){PJSON_STATUS_ERROR,
				                      PJSON_EVENT_ERROR_STACK_LIMIT};

			pj_stack_set(ctx, top, PJ_PARSER_STATE_MEMBER_COLON);
			return (pjson_result){PJSON_STATUS_OKAY};
		case PJ_LEXER_EVENT_TOKEN_RIGHT_CURLY:
			pj_stack_pop(ctx);
			return lexer_res;
		default:
			return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_TOKEN};
		}
	case PJ_PARSER_STATE_MEMBER_STRING:
		if (pj_unlikely(lexer_res.event != PJ_LEXER_EVENT_BEGIN_STRING))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_EXPECT_STRING};

		if (pj_unlikely(pj_stack_push(ctx, PJ_PARSER_STATE_WAIT_TOKEN)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_STACK_LIMIT};

		pj_stack_set(ctx, top, PJ_PARSER_STATE_MEMBER_COLON);
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_PARSER_STATE_MEMBER_COLON:
		if (pj_unlikely(lexer_res.event != PJ_LEXER_EVENT_TOKEN_COLON))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_EXPECT_COLON};

		if (pj_unlikely(pj_stack_push(ctx, PJ_PARSER_STATE_VALUE)))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_STACK_LIMIT};

		pj_stack_set(ctx, top, PJ_PARSER_STATE_MEMBER_AFTER);
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_PARSER_STATE_MEMBER_AFTER:
		switch (lexer_res.event) {
		case PJ_LEXER_EVENT_TOKEN_COMMA:
			pj_stack_set(ctx, top, PJ_PARSER_STATE_MEMBER_STRING);
			return (pjson_result){PJSON_STATUS_OKAY};
		case PJ_LEXER_EVENT_TOKEN_RIGHT_CURLY:
			pj_stack_pop(ctx);
			return lexer_res;
		default:
			return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_TOKEN};
		}

	case PJ_PARSER_STATE_DONE:
		if (pj_unlikely(lexer_res.event != PJ_LEXER_EVENT_DONE))
			return (pjson_result){PJSON_STATUS_ERROR,
			                      PJSON_EVENT_ERROR_EXPECT_DONE};

		return (pjson_result){PJSON_STATUS_DONE};

	pj_nodefault
	}

	pj_unreachable();
}


/*
 * API.
 */

inline static void pj_reset(pjson_context *ctx)
{
	ctx->utf8_state  = PJ_UTF8_STATE_BOM_0;
	ctx->lexer_state = PJ_LEXER_STATE_INITIAL;
	ctx->byte_count  = 0;
	ctx->stack_top   = 1;
	pj_stack_set(ctx, 1, PJ_PARSER_STATE_VALUE);
}

PJSON_API void pjson_init(pjson_context *restrict ctx, pjson_block block)
{
	pj_assert((block.size >= PJ_MIN_STACK) & (block.ptr != (void *)0));

	ctx->high       = 0;
	ctx->low        = 0;
	ctx->utf8_buf   = 0;
	ctx->stack_size = block.size;
	ctx->stack      = block.ptr;
	pj_stack_set(ctx, 0, PJ_PARSER_STATE_DONE);  /* This is never popped. */
	pj_reset(ctx);
}

PJSON_API pjson_result pjson_push(pjson_context *ctx, int byte)
{
	pj_assume(((byte >= 0) & (byte <= 0xFF)) | (byte == PJSON_END));

	unsigned char utf8_state = ctx->utf8_state;
	pjson_codepoint utf8_buf = ctx->utf8_buf;

	pj_utf8_result utf8_res = pj_utf8_push(ctx, (unsigned)byte);
	switch (utf8_res.status) {
	case PJ_UTF8_ACCEPT:
		++(ctx->byte_count);  /* Happens with `PJSON_END` too. */
		break;
	case PJ_UTF8_OKAY:
		++(ctx->byte_count);
		return (pjson_result){PJSON_STATUS_OKAY};
	case PJ_UTF8_ERROR:
		return (pjson_result){PJSON_STATUS_ERROR, PJSON_EVENT_ERROR_UTF8};
	pj_nodefault
	}

	unsigned char lexer_state = ctx->lexer_state;
	pjson_surrogate high = ctx->high;
	pjson_surrogate low = ctx->low;

	pjson_result lexer_res = pj_lexer_push(ctx, utf8_res.codepoint);
	switch (lexer_res.status) {
	case PJSON_STATUS_ACCEPT_RETRY:
		/*
		 * Caller needs to re-push the byte. Rollback UTF-8 state and decrement
		 * byte count.
		 */
		ctx->utf8_state = utf8_state;
		ctx->utf8_buf = utf8_buf;
		--(ctx->byte_count);
		pj_fallthrough;
	case PJSON_STATUS_ACCEPT:
		break;

	default:
		return lexer_res;
	}

	pjson_result res = pj_parser_push(ctx, lexer_res);
	if (pj_unlikely(res.status == PJSON_STATUS_ERROR)) {
		if (lexer_res.status != PJSON_STATUS_ACCEPT_RETRY) {
			ctx->utf8_state = utf8_state;
			ctx->utf8_buf = utf8_buf;
			--(ctx->byte_count);
		}

		/*
		 * Also reset the lexer's state in case of error (e.g. might have
		 * reached the stack limit).
		 */
		ctx->lexer_state = lexer_state;
		ctx->high = high;
		ctx->low = low;
	}

	return res;
}

PJSON_API pjson_result pjson_push_codepoint(pjson_context *ctx,
                                            pjson_codepoint codepoint)
{
	unsigned char lexer_state = ctx->lexer_state;
	pjson_surrogate high = ctx->high;
	pjson_surrogate low = ctx->low;

	pjson_usize code_size;
	pjson_result lexer_res = pj_lexer_push(ctx, codepoint);
	switch (lexer_res.status) {
	case PJSON_STATUS_ACCEPT:
		code_size = pj_utf8_code_size(codepoint);
		ctx->byte_count += code_size;
		break;
	case PJSON_STATUS_ACCEPT_RETRY:
		code_size = 0;
		break;
	default:
		return lexer_res;
	}

	pjson_result res = pj_parser_push(ctx, lexer_res);
	if (pj_unlikely(res.status == PJSON_STATUS_ERROR)) {
		ctx->lexer_state = lexer_state;
		ctx->high = high;
		ctx->low = low;
		ctx->byte_count -= code_size;
	}

	return res;
}

PJSON_API enum pjson_state pjson_current_state(const pjson_context *ctx)
{
	static const unsigned char to_pub[] = {
#		if 0
		[PJ_PARSER_STATE_VALUE            ] = PJSON_STATE_NONE,
		[PJ_PARSER_STATE_WAIT_TOKEN       ] = PJSON_STATE_NONE,
#		endif
		[PJ_PARSER_STATE_ELEMENT_LIST  - 2] = PJSON_STATE_IN_ARRAY,
		[PJ_PARSER_STATE_ELEMENT_AFTER - 2] = PJSON_STATE_IN_ARRAY,
		[PJ_PARSER_STATE_MEMBER_LIST   - 2] = PJSON_STATE_IN_OBJECT,
		[PJ_PARSER_STATE_MEMBER_STRING - 2] = PJSON_STATE_IN_OBJECT,
		[PJ_PARSER_STATE_MEMBER_COLON  - 2] = PJSON_STATE_IN_KEY,
		[PJ_PARSER_STATE_MEMBER_AFTER  - 2] = PJSON_STATE_IN_OBJECT,
		[PJ_PARSER_STATE_DONE          - 2] = PJSON_STATE_NONE
	};

	pjson_usize top = ctx->stack_top;
	enum pj_parser_state top_val = pj_stack_get(ctx, top);
	if (top_val <= PJ_PARSER_STATE_WAIT_TOKEN)
		top_val = pj_stack_get(ctx, top - 1);

	return (enum pjson_state)to_pub[top_val - 2];
}

PJSON_API void pjson_resize(pjson_context *restrict ctx, pjson_block block)
{
	pj_assert((block.size >= PJ_MIN_STACK) & (block.ptr != (void *)0));

	ctx->stack_size = block.size;
	ctx->stack = block.ptr;
}

PJSON_API void pjson_reset(pjson_context *ctx)
{
	pj_reset(ctx);
}

PJSON_API pjson_block pjson_context_block(const pjson_context *ctx)
{
	return (pjson_block){ctx->stack_size, ctx->stack};
}

PJSON_API pjson_usize pjson_position(const pjson_context *ctx)
{
	return ctx->byte_count;
}

PJSON_API pjson_usize pjson_stack_used(const pjson_context *ctx)
{
	return pj_stack_used(ctx);
}


/*
 * TODO: Could probably take advantage of the padding inside `pjson_context`
 *       and provide a very tiny (2-4 bytes) "inline" stack buffer by default.
 */
