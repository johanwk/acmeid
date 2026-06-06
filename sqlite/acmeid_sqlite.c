/*
 * acmeid_sqlite.c -- SQLite loadable extension exposing
 *
 *     acme_mint_id(type)
 *     acme_mint_id(type, prefix)
 *     acme_mint_id(type, prefix, label)
 *     acme_mint_id(type, prefix, label, length)
 *     acme_verify_id(id)
 *
 * Load with:
 *     .load /usr/local/lib/acmeid                  (Linux/macOS)
 *     .load c:/opt/acmeid/acmeid                   (MSYS2/Windows)
 *
 * IMPORTANT: All four arities of acme_mint_id are registered WITHOUT
 * the SQLite determinism flag.  Setting it would let the planner
 * cache the result of a single mint() call and reuse it across rows
 * -- silently producing duplicate IDs in any
 *
 *     INSERT ... SELECT acme_mint_id(...) FROM ...
 *
 * statement.  Only acme_verify_id is marked deterministic, so that it
 * can appear in CHECK constraints, generated columns, and indexes.
 *
 * The Makefile audit target greps this file for the determinism flag
 * token and requires exactly one occurrence (on the verify registration
 * line below).  Do not mention the flag's literal name elsewhere --
 * describe the design in prose as the comment above does.
 */

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include "acmeid.h"

#include <ctype.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  UDF implementations                                               */
/* ------------------------------------------------------------------ */

static void mint_impl(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    /* TYPE (required). */
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_error(ctx, "acme_mint_id: type is NULL", -1);
        return;
    }
    const unsigned char *type_text = sqlite3_value_text(argv[0]);
    if (!type_text || !type_text[0] || !isalpha(type_text[0])) {
        sqlite3_result_error(ctx,
            "acme_mint_id: type must be a single ASCII letter", -1);
        return;
    }
    char type = (char)type_text[0];

    /* PREFIX (arg 1, optional). NULL or "" -> absent. */
    const char *prefix = NULL;
    if (argc >= 2 && sqlite3_value_type(argv[1]) != SQLITE_NULL) {
        const char *s = (const char *)sqlite3_value_text(argv[1]);
        if (s && s[0]) prefix = s;
    }

    /* LABEL (arg 2, optional). NULL or "" -> no slug. */
    const char *label = NULL;
    if (argc >= 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
        const char *s = (const char *)sqlite3_value_text(argv[2]);
        if (s && s[0]) label = s;
    }

    /* LENGTH (arg 3, optional). Negative/zero -> default (4); the
     * core clamps silently to [2,8]. */
    int rand_len = 4;
    if (argc >= 4 && sqlite3_value_type(argv[3]) != SQLITE_NULL) {
        rand_len = sqlite3_value_int(argv[3]);
    }

    char out[ACME_MAX_ID_LEN];
    int n = acme_mint_id(type, prefix, label, rand_len, out, sizeof(out));
    if (n < 0) {
        sqlite3_result_error(ctx, "acme_mint_id: mint failed", -1);
        return;
    }
    sqlite3_result_text(ctx, out, n, SQLITE_TRANSIENT);
}

static void verify_impl(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    (void)argc;
    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }
    const char *id = (const char *)sqlite3_value_text(argv[0]);
    sqlite3_result_int(ctx, acme_verify_id(id));
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                       */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_acmeid_init(sqlite3 *db, char **pzErrMsg,
                        const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);
    (void)pzErrMsg;

    int rc;

    /* acme_mint_id: arities 1..4, all NON-deterministic (see header). */
    for (int n = 1; n <= 4; n++) {
        rc = sqlite3_create_function(db, "acme_mint_id", n,
                                     SQLITE_UTF8,
                                     NULL, mint_impl, NULL, NULL);
        if (rc != SQLITE_OK) return rc;
    }

    /* acme_verify_id: arity 1, deterministic + innocuous so it can
     * appear in CHECK constraints, generated columns, and indexes. */
    rc = sqlite3_create_function(db, "acme_verify_id", 1,
                                 SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS,
                                 NULL, verify_impl, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    return SQLITE_OK;
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg,
                           const sqlite3_api_routines *pApi) {
    return sqlite3_acmeid_init(db, pzErrMsg, pApi);
}
