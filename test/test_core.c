/*
 * test/test_core.c -- Unit tests for the pure C library (src/acmeid.c).
 *
 * Pass/fail is reported per assertion; a non-zero exit code means at
 * least one assertion failed.  Designed to run under
 * -fsanitize=address,undefined on Linux/macOS.
 */

#include "acmeid.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do {                                                \
    if (expr) { g_pass++; }                                             \
    else {                                                              \
        g_fail++;                                                       \
        fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr);\
    }                                                                   \
} while (0)

#define CHECK_STREQ(a, b) do {                                          \
    if (strcmp((a), (b)) == 0) { g_pass++; }                            \
    else {                                                              \
        g_fail++;                                                       \
        fprintf(stderr, "FAIL [%s:%d] \"%s\" == \"%s\"\n",              \
                __FILE__, __LINE__, (a), (b));                          \
    }                                                                   \
} while (0)

/* --------------------------------------------------------- slug5 -- */

static void test_slug5(void) {
    char s[6];

    acme_slug5("Employment contract", s);  CHECK_STREQ(s, "emplo");
    acme_slug5("ISO 50001",           s);  CHECK_STREQ(s, "isoxx");
    acme_slug5("",                    s);  CHECK_STREQ(s, "xxxxx");
    acme_slug5(NULL,                  s);  CHECK_STREQ(s, "xxxxx");

    /* UTF-8 Latin-1 supplement folding. */
    acme_slug5("\xC3\x85ngstr\xC3\xB6m", s); CHECK_STREQ(s, "angst"); /* Ångström */
    acme_slug5("Na\xC3\xAFve",           s); CHECK_STREQ(s, "naive"); /* Naïve   */
    /* Literal split: \xA5 would otherwise greedily absorb the 'f' as a hex digit. */
    acme_slug5("Sm\xC3\xA5" "folk",     s); CHECK_STREQ(s, "smafo"); /* Småfolk */
}

/* ------------------------------------------------------ mint/verify */

static void test_mint_verify_round_trip(void) {
    char id[ACME_MAX_ID_LEN];

    /* Arity 1: type only.  Expect "T_TTTTRRRRC" = 11 chars. */
    int n = acme_mint_id('C', NULL, NULL, 0, id, sizeof(id));
    CHECK(n == 11);
    CHECK(acme_verify_id(id) == 1);

    /* Arity 2: type + prefix.  Prefix is not in checksum. */
    n = acme_mint_id('C', "ex:", NULL, 0, id, sizeof(id));
    CHECK(n > 0);
    CHECK(strncmp(id, "ex:", 3) == 0);
    CHECK(acme_verify_id(id) == 1);
    CHECK(acme_verify_id(id + 3) == 1);  /* strip prefix, still valid */

    /* Arity 3: with label.  Expect prefix + "T_sssssTTTTRRRRC". */
    n = acme_mint_id('C', "ex:", "Pitch 1.5 mm", 0, id, sizeof(id));
    CHECK(n > 0);
    CHECK(strstr(id, "C_pitch") != NULL);
    CHECK(acme_verify_id(id) == 1);

    /* Arity 4: explicit length. */
    n = acme_mint_id('C', "ex:", "Pitch", 6, id, sizeof(id));
    CHECK(n > 0);
    /* Total body length (after prefix) = 2 + 5 + 4 + 6 + 1 = 18; prefix "ex:" = 3. */
    CHECK(strlen(id) == 3u + 18u);
    CHECK(acme_verify_id(id) == 1);
}

static void test_mint_silent_clamp(void) {
    char id[ACME_MAX_ID_LEN];
    /* rand_len: 0, 1, 9, 100 -- all should produce valid IDs. */
    int lens[] = {0, 1, 2, 8, 9, 100, -5};
    for (size_t i = 0; i < sizeof(lens)/sizeof(lens[0]); i++) {
        CHECK(acme_mint_id('C', NULL, NULL, lens[i], id, sizeof(id)) > 0);
        CHECK(acme_verify_id(id) == 1);
    }
}

static void test_mint_bad_type(void) {
    char id[ACME_MAX_ID_LEN];
    CHECK(acme_mint_id('1', NULL, NULL, 4, id, sizeof(id)) == -1);
    CHECK(acme_mint_id('!', NULL, NULL, 4, id, sizeof(id)) == -1);
    CHECK(acme_mint_id(0,   NULL, NULL, 4, id, sizeof(id)) == -1);
}

static void test_mint_buffer_overflow(void) {
    char id[8];   /* far too small */
    CHECK(acme_mint_id('C', NULL, "label", 4, id, sizeof(id)) == -1);
    CHECK(id[0] == '\0');
}

/* ------------------------------------------------- negative verify */

static void test_verify_negative(void) {
    char id[ACME_MAX_ID_LEN];
    CHECK(acme_mint_id('C', NULL, "Pitch", 4, id, sizeof(id)) > 0);
    CHECK(acme_verify_id(id) == 1);

    /* Tampering: bump the last char by one Crockford step. */
    char broken[ACME_MAX_ID_LEN];
    strcpy(broken, id);
    size_t last = strlen(broken) - 1;
    broken[last] = (broken[last] == 'Z') ? '0' : broken[last] + 1;
    CHECK(acme_verify_id(broken) == 0);

    /* Lowercase TYPE. */
    strcpy(broken, id);
    broken[0] = (char)tolower((unsigned char)broken[0]);
    CHECK(acme_verify_id(broken) == 0);

    /* Truncation.  Chopping only the 1-char CHK leaves a 15-char string
     * that is itself a valid v1.1 shape (rand_len = 3); in 1/32 of cases
     * the residual sum is also 0 mod 32 and verify accidentally passes.
     * Remove 3 chars instead -- that drops length below the with-slug
     * minimum (14), so the shape check rejects every time. */
    strcpy(broken, id);
    broken[strlen(broken) - 3] = '\0';
    CHECK(acme_verify_id(broken) == 0);

    /* NULL. */
    CHECK(acme_verify_id(NULL) == 0);

    /* Empty. */
    CHECK(acme_verify_id("") == 0);

    /* Bad prefix-only string. */
    CHECK(acme_verify_id("ex:") == 0);
}

static void test_verify_prefix_tolerant(void) {
    char id[ACME_MAX_ID_LEN];
    CHECK(acme_mint_id('C', NULL, NULL, 4, id, sizeof(id)) > 0);

    /* Generous: needs to hold a full URL prefix plus a max-length id. */
    char with_prefix[ACME_MAX_ID_LEN + 64];
    snprintf(with_prefix, sizeof(with_prefix), "ex:%s", id);
    CHECK(acme_verify_id(with_prefix) == 1);

    snprintf(with_prefix, sizeof(with_prefix), "https://x/y/%s", id);
    CHECK(acme_verify_id(with_prefix) == 1);
}

/* ----------------------------------------- entropy / no-duplicates */

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void test_no_duplicates(void) {
    enum { N = 5000 };
    char *ids = malloc(N * ACME_MAX_ID_LEN);
    char **ptrs = malloc(N * sizeof(*ptrs));
    if (!ids || !ptrs) { free(ids); free(ptrs); g_fail++; return; }

    for (int i = 0; i < N; i++) {
        ptrs[i] = ids + i * ACME_MAX_ID_LEN;
        CHECK(acme_mint_id('C', NULL, NULL, 4, ptrs[i], ACME_MAX_ID_LEN) > 0);
    }
    qsort(ptrs, N, sizeof(*ptrs), cmp_str);
    int dupes = 0;
    for (int i = 1; i < N; i++) {
        if (strcmp(ptrs[i-1], ptrs[i]) == 0) dupes++;
    }
    /* With 20 bits of entropy a few collisions in 5000 are statistically
     * possible (~6 expected); fail only on clearly broken RNG behaviour. */
    CHECK(dupes < 50);

    free(ids); free(ptrs);
}

/* ----------------------------------------------------------- main */

int main(void) {
    test_slug5();
    test_mint_verify_round_trip();
    test_mint_silent_clamp();
    test_mint_bad_type();
    test_mint_buffer_overflow();
    test_verify_negative();
    test_verify_prefix_tolerant();
    test_no_duplicates();

    printf("test_core: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
