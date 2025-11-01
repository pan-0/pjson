/* Copyright 2025, pan (pan_@disroot.org) */
/* SPDX-License-Identifier: MIT-0 */

/*
 * This example, albeit contrived, demonstrates how one could go about creating
 * a DOM. It's a _lot_ of lines of code, but most of it is boilerplate;
 * helper macros, a linear allocator, data structures, workarounds for libc's
 * deficiencies, etc.
 *
 * A whole JSON document is read into memory, then parsed into a DOM.
 * Afterwards, the DOM gets traversed to be pretty-printed back as JSON to
 * `stdout`.
 */

#define PJSON_API static
#include "pjson.c"

#include <assert.h>   /* assert() */
#include <errno.h>    /* errno */
#include <stdbool.h>  /* bool, true, false */
#include <stddef.h>   /* size_t, ptrdiff_t, NULL, [offsetof(), max_align_t] */
#include <stdlib.h>   /* malloc(), free(), strtod(), strtoll() */
#include <stdint.h>   /* SIZE_MAX, uintptr_t */
#include <string.h>   /* memcpy(), memset() */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  ____   ___ ___ _     _____ ____  ____  _        _  _____ _____   *
 * | __ ) / _ \_ _| |   | ____|  _ \|  _ \| |      / \|_   _| ____|  *
 * |  _ \| | | | || |   |  _| | |_) | |_) | |     / _ \ | | |  _|    *
 * | |_) | |_| | || |___| |___|  _ <|  __/| |___ / ___ \| | | |___   *
 * |____/ \___/___|_____|_____|_| \_\_|   |_____/_/   \_\_| |_____|  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#if defined __GNUC__
#	define likely(x)     __builtin_expect(!!(x), 1)
#	define unlikely(x)   __builtin_expect(!!(x), 0)
#	define inline_never  __attribute__((__noinline__))
#	define inline_always __attribute__((__always_inline__)) inline
#	define allocalign(x) __attribute__((__alloc_align__(x)))
#	define fallthrough   __attribute__((__fallthrough__))
#elif defined _MSC_VER
#	define likely(x)     (x)
#	define unlikely(x)   (x)
#	define inline_never  __declspec(noinline)
#	define inline_always __forceinline
#	define allocalign(x)
#	define fallthrough
#else
#	define likely(x)     (x)
#	define unlikely(x)   (x)
#	define inline_never
#	define inline_always inline
#	define allocalign(x)
#	define fallthrough
#endif

#ifndef unreachable
#	define unreachable() assert(!"unreachable")
#endif

/* alignof */
#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 202311L  /* C23 */
	/* Keyword. */
#elif defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L  /* C18 or C11 */
#	define alignof _Alignof
#elif defined __GNUC__
#	define alignof __alignof__
#elif defined _MSC_VER
#	define alignof __alignof
#elif 1
#	define alignof(T) offsetof(struct { char _c; T _o; }, _o)
#endif

/* ALIGNMENT */
#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L  /* atleast C11 */
#	define ALIGNMENT alignof(max_align_t)
#else
#	define ALIGNMENT alignof(long double)
#endif

typedef size_t usize;


/*
 * BLayout.
 *
 * See <https://github.com/pan-0/BLayout>.
 */

struct blayout {
	usize nmemb;
	usize size;
	usize alignment;
};

inline static usize blcalc(usize alignment,
                           ptrdiff_t offs,
                           usize n,
                           const struct blayout *lays,
                           usize prev_size)
{
	usize i;
	usize base = alignment + (usize)offs;
	usize pos = base;
	if (pos + prev_size < pos)
		return 0;

	pos += prev_size;
	for (i = 0; i < n; ++i) {
		usize size;
		usize pad;
		struct blayout l = lays[i];
		if (l.nmemb > SIZE_MAX / l.size)
			return 0;

		size = l.nmemb * l.size;
		pad = ~(pos - 1) & (l.alignment - 1);
		if (size + pad < size)
			return 0;

		size += pad;
		if (pos + size < pos)
			return 0;

		pos += size;
	}

	return pos - base;
}

allocalign(3)
inline static void *blnext(void *ptr, usize curr_size, usize next_align)
{
	ptr = (char *)ptr + curr_size;
	return (char *)ptr + (usize)(~((uintptr_t)ptr - 1) & (next_align - 1));
}


/*
 * Linear allocator.
 */

typedef struct lina_node {
	struct lina_node *prev;
	void *pos;
	void *end;
} lina_node;

typedef struct {
	lina_node *curr;
	lina_node *head;
	usize size;
} lina;

static const lina_node lina_sentinel = {.prev=(lina_node *)&lina_sentinel,
                                        .pos =(void *)&lina_sentinel,
                                        .end =(void *)&lina_sentinel};

inline static usize lina_node_padding(const lina_node *node, usize alignment)
{
	return ~((uintptr_t)node->pos - 1) & (alignment - 1);
}

inline static usize lina_node_avail(const lina_node *node)
{
	return (usize)((char *)node->end - (char *)node->pos);
}

inline static void *lina_node_begin(const lina_node *node)
{
	return (char *)node + sizeof(*node);
}

inline static usize lina_node_offset(const lina_node *node)
{
	return (usize)((char *)node->pos - (char *)lina_node_begin(node));
}

allocalign(3) inline_never
static void *lina_alloc_slow(lina *lin, usize size, usize alignment)
{
	void *ptr;

	lina_node *curr = lin->curr;
	lina_node *node = curr->prev;
	usize padding = lina_node_padding(node, alignment);
	usize padded_size = padding + size;

	if (padded_size >= size && padded_size <= lina_node_avail(node)) {
		ptr = (char *)node->pos + padding;
		node->pos = (char *)ptr + size;
	}
	else {
		const struct blayout lays[] = {
			{1, sizeof(lina_node), alignof(lina_node)},
			{1, size, alignment}
		};
		usize min_size = blcalc(ALIGNMENT, 0, 2, lays, 0);
		if (unlikely(min_size == 0))
			return NULL;

		/* Align up to the desired node size. */
		usize block_size = min_size + (~(min_size - 1) & (lin->size - 1));
		if (block_size < min_size)
			block_size = min_size;

		node = malloc(block_size);
		if (unlikely(node == NULL))
			return NULL;

		char *begin = lina_node_begin(node);
		*node = (lina_node){.prev=lin->head,
		                    .pos=begin,
		                    .end=(char *)node + block_size};

		padding = lina_node_padding(node, alignment);
		ptr = begin + padding;
		node->pos = begin + padded_size;

		lin->head = node;
	}

	lin->curr = node;
	return ptr;
}

allocalign(3)
inline_always static void *lina_alloc(lina *lin, usize size, usize alignment)
{
	lina_node *curr = lin->curr;
	usize padding = lina_node_padding(curr, alignment);
	usize padded_size = padding + size;
	if (unlikely(padded_size < size || lina_node_avail(curr) < padded_size))
		return lina_alloc_slow(lin, size, alignment);

	void *ptr = (char *)curr->pos + padding;
	curr->pos = (char *)ptr + size;
	return ptr;
}

allocalign(5) inline_never static void *lina_grow_slow(lina *lin,
                                                       void *ptr,
                                                       usize size,
                                                       usize new_size,
                                                       usize alignment)
{
	void *new_ptr = lina_alloc_slow(lin, new_size, alignment);
	if (unlikely(new_ptr == NULL))
		return NULL;

	return memcpy(new_ptr, ptr, size);
}

allocalign(5) inline static void *lina_grow(lina *lin,
                                            void *ptr,
                                            usize size,
                                            usize new_size,
                                            usize alignment)
{
	assert(new_size > size);

	lina_node *curr = lin->curr;
	void *pos = curr->pos;
	if (unlikely(size > lina_node_offset(curr) || ptr != (char *)pos - size))
		return lina_grow_slow(lin, ptr, size, new_size, alignment);

	usize delta = new_size - size;
	if (unlikely(lina_node_avail(curr) < delta))
		return lina_grow_slow(lin, ptr, size, new_size, alignment);

	curr->pos = (char *)pos + delta;
	return ptr;
}

inline static void lina_shrink(lina *lin,
                               void *ptr,
                               usize size,
                               usize new_size,
                               usize alignment)
{
	assert(size > new_size);
	(void)alignment;

	lina_node *curr = lin->curr;
	void *pos = curr->pos;
	if (unlikely(size > lina_node_offset(curr) || ptr != (char *)pos - size))
		return;

	curr->pos = (char *)ptr + new_size;
}

inline
static void lina_dealloc(lina *lin, void *ptr, usize size, usize alignment)
{
	lina_shrink(lin, ptr, size, 0, alignment);
}

inline static lina lina_new(usize size)
{
	return (lina){(lina_node *)&lina_sentinel,
	              (lina_node *)&lina_sentinel,
	              size};
}

static void lina_fini(lina *lin)
{
	lina_node *node = lin->head;
	while (node != &lina_sentinel) {
		lina_node *curr = node;
		node = node->prev;
		free(curr);
	}
}


/*
 * Circular linked list.
 */

typedef struct list_node {
	struct list_node *next;
	/* T elem; */
} list_node;

typedef struct {
	/*
	 * +-> head -> ... -> tail -+
	 * |                        |
	 * +------------------------+
	 */
	list_node *tail;
} list;

inline static list list_new(void)
{
	return (list){NULL};
}

inline static void *list_push(list *lst, struct blayout elem_lay, lina *lin)
{
	const struct blayout lays[] = {
		{1, sizeof(list_node), alignof(list_node)},
		elem_lay
	};
	usize size = blcalc(alignof(list_node), 0, 2, lays, 0);
	if (unlikely(size == 0))
		return NULL;

	list_node *node = lina_alloc(lin, size, alignof(list_node));
	if (unlikely(node == NULL))
		return NULL;

	list_node *tail = lst->tail;
	if (unlikely(tail == NULL)) {
		node->next = node;
	}
	else {
		node->next = tail->next;
		tail->next = node;
	}
	lst->tail = node;
	return blnext(node, sizeof(*node), elem_lay.alignment);
}

inline static list_node *list_begin(const list *lst)
{
	list_node *tail = lst->tail;
	assert(tail != NULL);
	return tail->next;
}

allocalign(2) inline static void *list_first(const list *lst, usize alignment)
{
	list_node *begin = list_begin(lst);
	assert(begin != NULL);
	return blnext(begin, sizeof(begin), alignment);
}

allocalign(2) inline static void *list_last(const list *lst, usize alignment)
{
	list_node *tail = lst->tail;
	assert(tail != NULL);
	return blnext(tail, sizeof(*tail), alignment);
}

#define list_value(T, node) ((T *) blnext((node), sizeof(*(node)), alignof(T)))

#define list_for_at(T, lst, begin, ...)  \
    do {                                 \
        list_node *tail = (lst)->tail;   \
        assert(tail != NULL);            \
        list_node *node = (begin);       \
        do {                             \
            node = node->next;           \
            T *it = list_value(T, node); \
            __VA_ARGS__;                 \
        } while (node != tail);          \
    } while (0)

#define list_for(T, lst, ...)            \
    do {                                 \
        list_node *tail = (lst)->tail;   \
        if (unlikely(tail == NULL))      \
            break;                       \
                                         \
        list_node *node = tail;          \
        do {                             \
            node = node->next;           \
            T *it = list_value(T, node); \
            __VA_ARGS__;                 \
        } while (node != tail);          \
    } while (0)


/*
 * Byte array.
 */

typedef struct {
	unsigned char *restrict buf;
	unsigned char *restrict end;
	unsigned char *restrict lim;
} bytearray;

static const unsigned char bytearray_sentinel = 0;

inline static usize bytearray_avail(const bytearray *ba)
{
	return (usize)(ba->lim - ba->end);
}

inline static usize bytearray_capacity(const bytearray *ba)
{
	return (usize)(ba->lim - ba->buf);
}

inline static usize bytearray_size(const bytearray *ba)
{
	return (usize)(ba->end - ba->buf);
}

inline_never static bool bytearray_push_n_slow(bytearray *ba,
                                               usize n,
                                               const void *bytes,
                                               lina *lin)
{
	usize cap = bytearray_capacity(ba);
	if (unlikely(cap == SIZE_MAX))
		return true;

	usize avail = bytearray_avail(ba);
	usize need = n - avail;
	usize min_cap = cap + need;

	usize new_cap;
	if (cap == 0) {
		new_cap = 32;
	}
	else {
		new_cap = cap;
		do {
			usize doubled = new_cap << 1;
			if (doubled <= new_cap) {
				new_cap = SIZE_MAX;
				break;
			}
			new_cap = doubled;
		} while (new_cap < min_cap);
	}

	usize size = bytearray_size(ba);

	unsigned char *restrict new_buf;
	unsigned char *restrict buf = ba->buf;
	if (unlikely(buf == &bytearray_sentinel))
		new_buf = lina_alloc(lin, new_cap, 1);
	else
		new_buf = lina_grow(lin, buf, cap, new_cap, 1);

	if (unlikely(new_buf == NULL))
		return true;

	unsigned char *restrict end = new_buf + size;
	memcpy(end, bytes, n);

	ba->buf = new_buf;
	ba->end = end + n;
	ba->lim = new_buf + new_cap;
	return false;
}

inline static
bool bytearray_push_n(bytearray *ba, usize n, const void *bytes, lina *lin)
{
	assert(n != 0);

	if (bytearray_avail(ba) < n)
		return bytearray_push_n_slow(ba, n, bytes, lin);

	memcpy(ba->end, bytes, n);
	ba->end += n;
	return false;
}

static void bytearray_shrink(bytearray *ba, lina *lin)
{
	if (ba->end != ba->lim) {
		usize cap  = bytearray_capacity(ba);
		usize size = bytearray_size(ba);
		lina_shrink(lin, ba->buf, cap, size, 1);
		ba->lim = ba->end;
	}
}

static void bytearray_fini(bytearray *ba, lina *lin)
{
	lina_dealloc(lin, ba->buf, bytearray_capacity(ba), 1);
}

inline static bytearray bytearray_new(void)
{
	return (bytearray){(unsigned char *)&bytearray_sentinel,
	                   (unsigned char *)&bytearray_sentinel,
	                   (unsigned char *)&bytearray_sentinel};
}


/*
 * Number conversions.
 */

typedef struct {
	bool err;
	long long val;
} atoll_res;

typedef struct {
	bool err;
	double val;
} atod_res;

static int errno_helper(int ne)
{
	int e = errno;
	errno = (ne <= 0 ? 0 : ne);
	return e;
}

static atoll_res atollx(const void *str)
{
	int err = errno_helper(0);
	char *endptr;
	long long val = strtoll(str, &endptr, 10);
	err = errno_helper(err);
	if (err != 0 || endptr == str)
		return (atoll_res){.err=true};

	return (atoll_res){.err=false, .val=val};
}

static atod_res atod(const void *str)
{
	int err = errno_helper(0);
	char *endptr;
	double val = strtod(str, &endptr);
	err = errno_helper(err);
	if (err != 0 || endptr == str)
		return (atod_res){.err=true};

	return (atod_res){.err=false, .val=val};
}


/* * * * * * * * * * * * * * * * *
 *       _  _____  ____  _   _   *
 *      | |/ ____|/ __ \| \ | |  *
 *      | | (___ | |  | |  \| |  *
 *  _   | |\___ \| |  | | . ` |  *
 * | |__| |____) | |__| | |\  |  *
 *  \____/|_____/ \____/|_| \_|  *
 * * * * * * * * * * * * * * * * */

typedef struct object object;
typedef struct array  array;

enum value_kind {
	/* Reserved. */
	VALUE_FALSE = 1u,
	VALUE_TRUE,
	VALUE_NULL,
	VALUE_STRING,
	VALUE_FLOAT,
	VALUE_INTEGER,
	VALUE_OBJECT,
	VALUE_ARRAY
};

typedef unsigned long long value_tag;
typedef long long integer;

typedef struct {
	value_tag tag;  /* length : ... - 8, kind: 8; */
	union {
		double  fnum;
		integer inum;
		void    *str;
		list    array;   /* Of `value`. */
		list    object;  /* Of `member`. */
	} u;
} value;

typedef struct {
	usize size;
	void *data;
} string;

typedef struct {
	string key;
	value  val;
} member;

static const struct blayout value_lay  = {1, sizeof(value),  alignof(value)},
                            member_lay = {1, sizeof(member), alignof(member)};

#define array_list(list_func, ...)  list_func(value,  __VA_ARGS__)
#define object_list(list_func, ...) list_func(member, __VA_ARGS__)

inline static value_tag tag_with_length(enum value_kind kind, usize len)
{
	if (unlikely(len > ((value_tag)-1 >> 8)))
		return 0;

	return ((value_tag)len << 8) | kind;
}

inline static enum value_kind value_kind(value_tag tag)
{
	return (enum value_kind)(tag & 0xFF);
}

inline static usize value_length(value_tag tag)
{
	return (usize)(tag >> 8);
}

inline static string value_to_string(value val)
{
	assert(value_kind(val.tag) == VALUE_STRING);
	return (string){value_length(val.tag), val.u.str};
}

typedef struct {
	list  lst;
	usize len;
} entry;

inline static value *push(const pjson_context *ctx, entry *top, lina *lin)
{
	++(top->len);

	if (pjson_current_state(ctx) == PJSON_STATE_IN_OBJECT) {
		member *mem = list_last(&top->lst, alignof(member));
		assert(mem != NULL);
		return &mem->val;
	}

	return list_push(&top->lst, value_lay, lin);
}

static value *parse(usize size,
                    const void *buf,
                    usize dec_point_len,
                    const void *dec_point,
                    lina *lin)
{
	enum { stack_len = 128 };

	entry *stack = lina_alloc(lin, stack_len * sizeof(*stack), alignof(stack));
	if (unlikely(stack == NULL))
		return NULL;

	unsigned char *restrict parser_stack = lina_alloc(lin, stack_len, 1);
	if (unlikely(parser_stack == NULL))
		return NULL;

	memset(parser_stack, 0, stack_len);
	pjson_context ctx;
	pjson_init(&ctx, (pjson_block){stack_len, parser_stack});

	bytearray codes = bytearray_new();

	entry *top = stack;
	*top = (entry){list_new(), 0};

	usize i = 0;
	while (true) {
		int byte = i < size ? ((const unsigned char *)buf)[i] : PJSON_END;
		pjson_result res = pjson_push(&ctx, byte);
		switch (res.status) {
		case PJSON_STATUS_ACCEPT:
			++i;
			fallthrough;
		case PJSON_STATUS_ACCEPT_RETRY:
			switch (res.event) {
			case PJSON_EVENT_NULL:
			case PJSON_EVENT_TRUE:
			case PJSON_EVENT_FALSE:
				{
					static const unsigned char to_kind[] = {
						[PJSON_EVENT_NULL]  = VALUE_NULL,
						[PJSON_EVENT_TRUE]  = VALUE_TRUE,
						[PJSON_EVENT_FALSE] = VALUE_FALSE
					};
					value *val = push(&ctx, top, lin);
					if (unlikely(val == NULL))
						return NULL;

					val->tag = (enum value_kind)to_kind[res.event];
				}
				break;

			case PJSON_EVENT_BEGIN_OBJECT:
			case PJSON_EVENT_BEGIN_ARRAY:
				++top;
				if (unlikely(top == stack + stack_len))
					return NULL;

				*top = (entry){list_new(), 0};
				break;

			case PJSON_EVENT_END_ARRAY:
				{
					entry array = *top;
					--top;
					value *val = push(&ctx, top, lin);
					if (unlikely(val == NULL))
						return NULL;

					value_tag tag = tag_with_length(VALUE_ARRAY, array.len);
					if (unlikely(tag == 0))
						return NULL;

					val->tag = tag;
					val->u.array = array.lst;
				}
				break;

			case PJSON_EVENT_END_OBJECT:
				{
					entry object = *top;
					--top;
					value *val = push(&ctx, top, lin);
					if (unlikely(val == NULL))
						return NULL;

					value_tag tag = tag_with_length(VALUE_OBJECT, object.len);
					if (unlikely(tag == 0))
						return NULL;

					val->tag = tag;
					val->u.object = object.lst;
				}
				break;

			case PJSON_EVENT_NUMBER_CODE:
				if (res.code_bytes[0] == 0x002E) {  /* '.' */
					if (bytearray_push_n(&codes,
					                     dec_point_len,
					                     dec_point,
					                     lin))
						return NULL;

					break;
				}
				fallthrough;
			case PJSON_EVENT_STRING_CODE:
				if (bytearray_push_n(&codes,
				                     res.code_size,
				                     res.code_bytes,
				                     lin))
					return NULL;

				break;

			case PJSON_EVENT_END_STRING:
				bytearray_shrink(&codes, lin);
				if (pjson_current_state(&ctx) == PJSON_STATE_IN_KEY) {
					member *mem = list_push(&top->lst, member_lay, lin);
					if (unlikely(mem == NULL))
						return NULL;

					mem->key = (string){bytearray_size(&codes), codes.buf};
				}
				else {
					value *val = push(&ctx, top, lin);
					if (unlikely(val == NULL))
						return NULL;

					value_tag tag = tag_with_length(VALUE_STRING,
					                                bytearray_size(&codes));
					if (unlikely(tag == 0))
						return NULL;

					val->tag = tag;
					val->u.str = codes.buf;
				}
				codes = bytearray_new();
				break;

			case PJSON_EVENT_END_NUMBER_FLOAT:
				if (bytearray_push_n(&codes, 1, &bytearray_sentinel, lin))
					return NULL;

				{
					atod_res r = atod(codes.buf);
					if (unlikely(r.err))
						return NULL;

					bytearray_fini(&codes, lin);
					value *val = push(&ctx, top, lin);
					val->tag = VALUE_FLOAT;
					val->u.fnum = r.val;
				}
				codes = bytearray_new();
				break;

			case PJSON_EVENT_END_NUMBER_INTEGER:
				if (bytearray_push_n(&codes, 1, &bytearray_sentinel, lin))
					return NULL;

				{
					atoll_res r = atollx(codes.buf);
					if (unlikely(r.err))
						return NULL;

					bytearray_fini(&codes, lin);
					value *val = push(&ctx, top, lin);
					val->tag = VALUE_INTEGER;
					val->u.inum = r.val;
				}
				codes = bytearray_new();
				break;

			default:
				unreachable();
			}
			break;

		case PJSON_STATUS_OKAY:
			++i;
			break;

		case PJSON_STATUS_ERROR: return NULL;
		case PJSON_STATUS_DONE:  goto end;

		default:
			unreachable();
		}
	}

end:
	return list_first(&stack[0].lst, alignof(value));
}


/* * * * * * * * * * * * * * * * *
 *  __  __          _____ _   _  *
 * |  \/  |   /\   |_   _| \ | | *
 * | \  / |  /  \    | | |  \| | *
 * | |\/| | / /\ \   | | | . ` | *
 * | |  | |/ ____ \ _| |_| |\  | *
 * |_|  |_/_/    \_\_____|_| \_| *
 * * * * * * * * * * * * * * * * */

/*#include <assert.h>  / * assert() */
#include <locale.h>  /* localeconv() */
/*#include <stddef.h>  / * size_t, NULL */
/*
 * fopen(), fread(), fclose(), printf(), putchar(), fwrite(), ferror(),
 * stdout, stdin, FILE
 */
#include <stdio.h>
/*#include <stdlib.h>  / * free(), realloc(), EXIT_* */
/*#include <string.h>  / * strcmp(), strlen() */

#if 0
typedef size_t usize;
#ifndef unreachable
#	define unreachable() assert(!"unreachable")
#endif
#endif

typedef struct {
	usize size;
	void *data;
} filebuffer;

static filebuffer fload(FILE *f)
{
	enum { chunksize = 32 * 1024 };

	filebuffer buf = {0};
	usize read;
	do {
		usize size = buf.size;
		usize new_size = size + chunksize;
		if (unlikely(new_size < buf.size))
			goto err;

		void *new_data = realloc(buf.data, new_size);
		if (unlikely(new_data == NULL))
			goto err;

		buf.size = new_size;
		buf.data = new_data;
		read = fread((char *)buf.data + size, 1, chunksize, f);
		if (unlikely(ferror(f)))
			goto err;
	} while (read == chunksize);

	buf.size = buf.size - chunksize + read;
	if (buf.size == 0)
		goto err;

	return buf;

err:
	free(buf.data);
	return (filebuffer){0};
}

static void print_indent(unsigned level)
{
	while (level > 0) {
		putchar('\t');
		--level;
	}
}

static void print_string(string str)
{
	putchar('"');
	for (usize i = 0; i < str.size; ++i) {
		unsigned char byte = ((unsigned char *)str.data)[i];
		switch (byte) {
		case 0x0009: printf("\\t");  break;
		case 0x000A: printf("\\n");  break;
		case 0x000D: printf("\\r");  break;
		case 0x0022: printf("\\\""); break;
		case 0x005C: printf("\\\\"); break;
		default:
			if (byte <= 0x001F)
				printf("\\u%04X", byte);
			else
				putchar(byte);
			break;
		}
	}
	putchar('"');
}

static void print_value(const value *val, unsigned nest);

static void print_member(const member *mem, unsigned nest)
{
	print_string(mem->key);
	printf(": ");
	print_value(&mem->val, nest);
}

static void print_value(const value *val, unsigned nest)
{
	switch (value_kind(val->tag)) {
	case VALUE_NULL:
		printf("null");
		break;

	case VALUE_TRUE:
		printf("true");
		break;

	case VALUE_FALSE:
		printf("false");
		break;

	case VALUE_STRING:
		print_string(value_to_string(*val));
		break;

	case VALUE_INTEGER:
		printf("%lld", val->u.inum);
		break;

	case VALUE_FLOAT:
		printf("%.17g", val->u.fnum);
		break;

	case VALUE_ARRAY:
		switch (value_length(val->tag)) {
		case 0:
			putchar('[');
			break;
		case 1:
			putchar('[');
			{
				list array = val->u.array;
				list_node *begin = list_begin(&array);

				print_value(list_value(value, begin), nest);
			}
			break;
		default:
			printf("[\n");
			{
				list array = val->u.array;
				list_node *begin = list_begin(&array);

				print_indent(nest + 1);
				print_value(list_value(value, begin), nest + 1);

				array_list(list_for_at, &array, begin, {
					printf(",\n");
					print_indent(nest + 1);
					print_value(it, nest + 1);
				});
				putchar('\n');
			}
			print_indent(nest);
			break;
		}
		putchar(']');
		break;

	case VALUE_OBJECT:
		switch (value_length(val->tag)) {
		case 0:
			putchar('{');
			break;
		case 1:
			putchar('{');
			{
				list array = val->u.array;
				list_node *begin = list_begin(&array);

				member *mem = list_value(member, begin);
				print_member(mem, nest);
			}
			break;
		default:
			printf("{\n");
			{
				list object = val->u.object;
				list_node *begin = list_begin(&object);

				member *mem = list_value(member, begin);
				print_indent(nest + 1);
				print_member(mem, nest + 1);

				object_list(list_for_at, &object, begin, {
					printf(",\n");
					print_indent(nest + 1);
					print_member(it, nest + 1);
				});
				putchar('\n');
			}
			print_indent(nest);
			break;
		}
		putchar('}');
		break;

	default:
		unreachable();
	}
}

static void print_dom(const value *dom)
{
	print_value(dom, 0);
	putchar('\n');
}

int main(int argc, char *argv[])
{
	/* C locale braindeath. */
	const void *dec_point = localeconv()->decimal_point;
	usize dec_point_len = strlen(dec_point);

	int ret = EXIT_FAILURE;

	FILE *stream;
	if (argc != 2 || argv == NULL || argv[1] == NULL
			|| strcmp(argv[1], "-") == 0) {
		stream = stdin;
	}
	else {
		stream = fopen(argv[1], "rb");
		if (unlikely(stream == NULL))
			return ret;
	}

	filebuffer filebuf = fload(stream);
	if (stream != stdin)
		(void) fclose(stream);

	if (unlikely(filebuf.size == 0))
		return ret;

	lina lin = lina_new(32 * 1024);
	value *dom = parse(filebuf.size,
	                   filebuf.data,
	                   dec_point_len,
	                   dec_point,
	                   &lin);
	free(filebuf.data);
	if (dom == NULL)
		goto end;

	print_dom(dom);
	ret = EXIT_SUCCESS;

end:
	lina_fini(&lin);
	return ret;
}
