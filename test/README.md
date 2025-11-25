<!-- Copyright 2025, pan (pan_@disroot.org) -->
<!-- SPDX-License-Identifier: MIT-0 OR Apache-2.0 -->

```console
$ gmake tests
```
Assumes GCC/Clang, GNU Make and a POSIX environment.

The [`nst/`](./nst) directory contains the test suite found in [nst/JSONTestSuite](https://github.com/nst/JSONTestSuite) by Nicolas "nst" Seriot, licensed under the MIT License. See [`nst/LICENSE`](./nst/LICENSE) for details.

The [`jsc/`](./jsc) directory _would_ contain the [JSON_checker](https://www.json.org/JSON_checker/) test suite. It's licensed under an MIT-derived license, with the addition of a [curious](https://en.wikipedia.org/wiki/Douglas_Crockford#Software_license_for_%22Good,_not_Evil%22) clause. Due to this, no tests are vendored here, but you're welcome to add them on your own accord. Tests `fail1.json` and `fail18.json` are skipped due to relaxations in RFC 8259 and higher implementation limits; rename them to `_fail1.json` and `_fail18.json` respectively.

## Sanitizers

To test with a sanitized build, run:
```console
$ gmake clean; gmake BUILD=sanitize tests
```

### In parallel

Sanitized builds can benefit from parallelism. If you have `bash` and GNU Parallel installed, you can run the tests in parallel like so:
```console
$ gmake BUILD=sanitize RUNTESTS=partests.sh tests
```
