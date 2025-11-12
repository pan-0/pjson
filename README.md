<!-- Copyright 2025, pan (pan_@disroot.org) -->
<!-- SPDX-License-Identifier: MIT-0 OR Apache-2.0 -->

# pjson: Bare-bones JSON push parser for C99

The goal is a small, freestanding implementation that handles the most tedious parts of the specification (i.e. RFC 8259). Other goals were correctness and simplicity (it should be obvious what the parser does just from looking at the code).

The interface is "SAX"-style; the user receives back parsing events and is responsible for how to handle them. As such, the parser uses very little memory. It's also very slow. A fixed number of around ~`48` bytes for the internal state and `4` bits per parser state, the stack of which is provided by the user.

It's _very_ primitive, but quite workable for basic tasks like validation. With _some_ effort, one can build on top of this to create a DOM-style API; see [`examples/pjdom.c`](./examples/pjdom.c).

As mentioned, this implements RFC 8259 and _only it_. Only UTF-8 is supported. No other encodings, no JSON5, JSONC, JSONPath, JSON Schema, etc. Of note is that numbers aren't converted into binary form. Instead, the parser only validates their _syntax_ (that they correctly follow the grammar as in the RFC) and returns them back as-is. This is mainly because C's standard library offerings (`strtod()`) aren't suitable for this task, unfortunately. And a from-scratch implementation would more than _double_ the line count. Parsing floating point numbers turns out to be harder than parsing JSON.

If you're looking for a library that handles all those issues, offers better performance and probably has a nicer API, something else like `yyjson` is recommended.

# Basic validator example
```c
#include "pjson.h"
#include <assert.h>   /* assert() */
#include <stddef.h>   /* size_t */
#include <stdbool.h>  /* bool, true, false */

bool is_valid_json(size_t size, const void *buf)
{
    unsigned char stack_buf[128] = {0};  /* Must zero-initialize. */

    pjson_context ctx;
    pjson_init(&ctx, (pjson_block){sizeof stack_buf, stack_buf});

    size_t i = 0;
    while (true) {
        int byte = i < size ? ((const unsigned char *)buf)[i] : PJSON_END;
        pjson_result res;
    retry:
        res = pjson_push(&ctx, byte);
        switch (res.status) {
        case PJSON_STATUS_ACCEPT_RETRY:
            goto retry;

        case PJSON_STATUS_ACCEPT:  /* fallthrough; */
        case PJSON_STATUS_OKAY:
            ++i;
            break;

        case PJSON_STATUS_ERROR:
            return false;

        case PJSON_STATUS_DONE:
            goto valid;

        default:
            assert(!"unreachable");
        }
    }

valid:
    return true;
}
```

# Documentation
TODO

Until then, hopefully, [`pjson.h`](./pjson.h) and the programs in [`examples/`](./examples) clear up any confusion.

# LICENSE
This work is dual-licensed under the MIT No Attribution ("MIT-0") and Apache License 2.0 ("Apache-2.0") licenses.
You can choose between one of them if you use this work.

```
SPDX-License-Identifier: MIT-0 OR Apache-2.0
```
