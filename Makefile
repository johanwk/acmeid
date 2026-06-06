# acmeid -- Makefile
#
# Targets:
#   lib      -> libacmeid.a
#   cli      -> acmeid[.exe]
#   sqlite   -> acmeid.{so,dylib,dll}
#   test     -> build + run unit / CLI / SQL tests
#   install  -> copy artefacts (platform-conventional location)
#   clean    -> remove build artefacts
#   help     -> show this list
#
# Primary platform is MSYS2/mingw-w64 on Windows; Linux and macOS are
# also supported.  Detection happens via `uname -s`.

# ------------------------------------------------------------------ OS
UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -fPIC
CPPFLAGS+= -Isrc

VERSION := 0.1.0
CPPFLAGS+= -DACMEID_VERSION=\"$(VERSION)\"

# Platform-specific shared-library naming and link flags.
ifneq (,$(filter Linux%,$(UNAME_S)))
    EXEEXT      :=
    SHLIB       := acmeid.so
    LDFLAGS_SHL := -shared
    LIBS_SHL    :=
    INSTALL_BIN := /usr/local/bin
    INSTALL_LIB := /usr/local/lib
    SAN_FLAGS   := -fsanitize=address,undefined
endif
ifneq (,$(filter Darwin%,$(UNAME_S)))
    EXEEXT      :=
    SHLIB       := acmeid.dylib
    # SQLite extension symbols (sqlite3_*) are resolved at runtime by
    # the hosting sqlite3 binary; tell ld64 not to fail on them.
    LDFLAGS_SHL := -dynamiclib -undefined dynamic_lookup
    LIBS_SHL    :=
    INSTALL_BIN := /usr/local/bin
    INSTALL_LIB := /usr/local/lib
    SAN_FLAGS   := -fsanitize=address,undefined
endif
ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
    EXEEXT      := .exe
    SHLIB       := acmeid.dll
    LDFLAGS_SHL := -shared -static-libgcc -Wl,--out-implib,acmeid.lib
    LIBS_SHL    := -lbcrypt
    INSTALL_BIN := c:/opt/acmeid
    INSTALL_LIB := c:/opt/acmeid
    SAN_FLAGS   :=
endif

# CLI link flags (executable, not shared lib).
LIBS_BIN :=
ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
    LIBS_BIN := -lbcrypt
endif

# Sources
LIB_SRCS    := src/acmeid.c
LIB_OBJS    := $(LIB_SRCS:.c=.o)
CLI_SRCS    := cli/main.c
SQLITE_SRCS := sqlite/acmeid_sqlite.c

LIB_A       := libacmeid.a
CLI_BIN     := acmeid$(EXEEXT)

.PHONY: all lib cli sqlite test test-core test-cli test-sqlite \
        test-audit install clean help

all: lib cli sqlite

help:
	@echo "Targets: lib cli sqlite test install clean help"
	@echo "Detected OS: $(UNAME_S)  ->  SHLIB=$(SHLIB)  EXEEXT=$(EXEEXT)"

# ----------------------------------------------------------------- lib
lib: $(LIB_A)

$(LIB_A): $(LIB_OBJS)
	$(AR) rcs $@ $^

src/%.o: src/%.c src/acmeid.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# ----------------------------------------------------------------- cli
cli: $(CLI_BIN)

$(CLI_BIN): $(CLI_SRCS) $(LIB_A)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CLI_SRCS) $(LIB_A) -o $@ $(LIBS_BIN)

# -------------------------------------------------------------- sqlite
sqlite: $(SHLIB)

$(SHLIB): $(SQLITE_SRCS) $(LIB_A)
	$(CC) $(CFLAGS) $(CPPFLAGS) -DACMEID_BUILDING_DLL \
	    $(LDFLAGS_SHL) $(SQLITE_SRCS) $(LIB_A) -o $@ $(LIBS_SHL)

# ---------------------------------------------------------------- test
test: test-core test-cli test-sqlite test-audit

test-core: test/test_core
	./test/test_core

test/test_core: test/test_core.c $(LIB_A)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SAN_FLAGS) \
	    test/test_core.c $(LIB_A) -o $@ $(LIBS_BIN)

test-cli: $(CLI_BIN)
	sh test/test_cli.sh

test-sqlite: $(SHLIB)
	sh test/run_sqlite_tests.sh

# Determinism / network / sprintf audits (Q.1, Q.3, Q.4).
test-audit:
	@echo "[audit] SQLITE_DETERMINISTIC must appear exactly once (verify only)"
	@n=$$(grep -c SQLITE_DETERMINISTIC sqlite/acmeid_sqlite.c); \
	  if [ $$n -ne 1 ]; then \
	    echo "  FAIL: found $$n occurrences (expected 1)"; exit 1; \
	  else echo "  OK"; fi
	@echo "[audit] no bare sprintf()"
	@if grep -rnE '\bsprintf[[:space:]]*\(' src cli sqlite 2>/dev/null; then \
	    echo "  FAIL"; exit 1; else echo "  OK"; fi
	@echo "[audit] no network calls"
	@if grep -rnE '\b(socket|connect|getaddrinfo|curl_easy|HttpOpen)\b' \
	     src cli sqlite 2>/dev/null; then \
	    echo "  FAIL"; exit 1; else echo "  OK"; fi
	@echo "[audit] no srand/rand in library"
	@if grep -nE '\b(srand|[^_a-zA-Z]rand)\b' src/acmeid.c 2>/dev/null; then \
	    echo "  FAIL"; exit 1; else echo "  OK"; fi

# ------------------------------------------------------------- install
install: cli sqlite
	mkdir -p $(INSTALL_BIN) $(INSTALL_LIB)
	cp $(CLI_BIN) $(INSTALL_BIN)/
	cp $(SHLIB)   $(INSTALL_LIB)/

# --------------------------------------------------------------- clean
clean:
	rm -f $(LIB_OBJS) $(LIB_A) $(CLI_BIN) $(SHLIB) acmeid.lib
	rm -f test/test_core test/test_core.exe
	rm -f test/*.tmp test/*.actual
