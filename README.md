# acmeid

Small C library, CLI tool, and SQLite loadable extension for minting
and verifying **ACME Localname IDs** -- short, sortable, checksummed
local identifiers for the localname part of enterprise IRIs.

The format is:

```
[PREFIX:] TYPE _ [SLUG5] TIME4 RAND_N CHK1
```

- `PREFIX` -- optional CURIE-style prefix, e.g. `ex:`. Not part of
  the checksum; tolerated by `verify`.
- `TYPE` -- single uppercase ASCII letter, domain-owned.
- `SLUG5` -- optional 5 lowercase letters derived from the label.
- `TIME4` -- 4 Crockford Base32 chars: days since 2020-01-01 UTC.
- `RAND_N` -- N random Crockford Base32 chars (`N` in `[2,8]`, default 4).
- `CHK1` -- 1 Crockford Base32 check character; the full ID's
  character values must sum to 0 mod 32.

See [`docs/spec.org`](docs/spec.org) for the full specification and
[`docs/recipes.org`](docs/recipes.org) for the OTTR / spine-table
workflow that motivated the SQLite extension.

## Build

Requires a C11 compiler and GNU make. SQLite headers are *not*
required at build time -- the extension uses the SQLite extension
loader contract and resolves symbols from the host process.

```sh
make            # builds libacmeid.a, acmeid CLI, acmeid.{so|dylib|dll}
make test       # runs unit, CLI, SQL, and audit tests
make install    # /usr/local on POSIX; c:/opt/acmeid on MSYS2
```

## CLI

```sh
acmeid mint   -t C                            # bare type
acmeid mint   -t C -p ex:                  # type + prefix
acmeid mint   -t C -p ex: -l "Pitch 1.5 mm"
acmeid mint   -t C -p ex: -l "Pitch" -n 6  # 6 random chars
acmeid verify ex:C_pitchABCD1234X
acmeid batch  -t C -p ex: < labels.txt
```

## SQLite

```sql
.load /usr/local/lib/acmeid        -- or:  c:/opt/acmeid/acmeid
SELECT acme_mint_id('C');
SELECT acme_mint_id('C','ex:');
SELECT acme_mint_id('C','ex:','Pitch 1.5 mm');
SELECT acme_mint_id('C','ex:','Pitch',6);
SELECT acme_verify_id('ex:C_pitchABCD1234X');
```

`acme_mint_id` is registered **non-deterministic** on purpose, so it
mints once per row in an `INSERT ... SELECT`. `acme_verify_id` is
deterministic and innocuous, so it works inside `CHECK` constraints,
generated columns, and indexes.

## Security & privacy

- No network. The library and CLI never open a socket. The audit
  test in `make test` greps for `socket|connect|getaddrinfo|...`
  to enforce this.
- Cryptographic randomness: `BCryptGenRandom` on Windows,
  `arc4random_buf` on macOS/BSD, `/dev/urandom` elsewhere. No
  `rand()` / `srand()`.
- All functions in `src/acmeid.c` are reentrant.

## License

GPLv3 -- see [`LICENSE`](LICENSE).
