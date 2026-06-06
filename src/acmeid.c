/*
 * acmeid.c -- Pure C implementation of the ACME Localname ID format.
 *
 * stdio is used only for snprintf (formatting into caller-provided
 * buffers); no FILE* I/O is performed.  No filesystem paths in this
 * file (the RNG fallback opens /dev/urandom on POSIX, but that's the
 * *only* path string).  No network access.  All public functions are
 * reentrant.
 *
 * The only platform-conditional code is acme_random_bytes(), which
 * selects an OS-provided cryptographic RNG.  Everything else is
 * straight portable C11.
 */

#include "acmeid.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Random bytes -- only platform-conditional code in this library.   */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#  ifndef STATUS_SUCCESS
#    define STATUS_SUCCESS ((NTSTATUS)0)
#  endif
int acme_random_bytes(void *buf, size_t n) {
    if (BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != STATUS_SUCCESS) {
        return -1;
    }
    return 0;
}
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <stdlib.h>
int acme_random_bytes(void *buf, size_t n) {
    arc4random_buf(buf, n);
    return 0;
}
#else
#  include <stdio.h>
int acme_random_bytes(void *buf, size_t n) {
    FILE *fp = fopen("/dev/urandom", "rb");
    if (!fp) return -1;
    size_t r = fread(buf, 1, n, fp);
    fclose(fp);
    return (r == n) ? 0 : -1;
}
#endif

/* ------------------------------------------------------------------ */
/*  Crockford Base32 alphabet and per-character value for checksums.  */
/* ------------------------------------------------------------------ */

static const char ALPH[33] = ACME_ALPH;

/*
 * char_value: distinct integer for every legitimate character in an
 * ID.  Any single-character corruption changes the checksum.
 *
 *   0  - 31 : Crockford Base32 digits/letters
 *   32 - 57 : lowercase a-z (slug)
 *   58 - 61 : non-Crockford uppercase TYPE letters I, L, O, U
 *
 * Returns -1 for any other character.
 */
static int char_value(int c) {
    const char *pos;
    if ((pos = strchr(ALPH, c)) != NULL) return (int)(pos - ALPH);
    if (c >= 'a' && c <= 'z')            return 32 + (c - 'a');
    if (c == 'I') return 58;
    if (c == 'L') return 59;
    if (c == 'O') return 60;
    if (c == 'U') return 61;
    return -1;
}

/* Encode unsigned integer in Base32, left-padded with '0' to `width`. */
static void b32_encode(uint64_t n, int width, char *out) {
    char tmp[16];
    int i = 0;
    if (n == 0) {
        tmp[i++] = '0';
    } else {
        while (n > 0 && i < (int)sizeof(tmp)) {
            tmp[i++] = ALPH[n % 32];
            n /= 32;
        }
    }
    int pad = (width > i) ? width - i : 0;
    int j = 0;
    for (int k = 0; k < pad; k++) out[j++] = '0';
    for (int k = i - 1; k >= 0; k--) out[j++] = tmp[k];
    out[j] = '\0';
}

/* Random Base32 string of given width (`width` in [1,8]). */
static int randb32(int width, char *out) {
    unsigned char bytes[8];
    if (acme_random_bytes(bytes, sizeof(bytes)) != 0) return -1;
    uint64_t n = 0;
    for (size_t i = 0; i < sizeof(bytes); i++) {
        n = (n << 8) | bytes[i];
    }
    int bits = width * 5;
    if (bits < 64) {
        n &= ((uint64_t)1 << bits) - 1;
    }
    b32_encode(n, width, out);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Slug folding                                                      */
/* ------------------------------------------------------------------ */

/*
 * fold_latin1: best-effort transliteration of UTF-8 2-byte sequences
 * starting with 0xC3 (Latin-1 supplement) to a single ASCII letter.
 * Returns 0 if the byte doesn't map to a letter.
 *
 * This is deliberately not a full Unicode NFKD normaliser -- those
 * require multi-kilobyte tables.  It covers the common Nordic and
 * Western European diacritics found in business labels, so:
 *
 *   "Ångström"          -> "angst"
 *   "Småfolk æbleskiver" -> "smafo"  (S, m, å->a, f, o)
 *   "Naïve"             -> "naive"
 *
 * Æ/æ collapse to 'e' (matching the elisp reference); two-letter
 * decompositions ("ae", "oe") are intentionally not produced.
 */
static char fold_latin1(unsigned char c) {
    switch (c) {
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: /* À-Å */
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: /* à-å */
        return 'a';
    case 0x86: case 0xA6:                                            /* Æ, æ */
        return 'e';
    case 0x87: case 0xA7:                                            /* Ç, ç */
        return 'c';
    case 0x88: case 0x89: case 0x8A: case 0x8B:                      /* È-Ë */
    case 0xA8: case 0xA9: case 0xAA: case 0xAB:                      /* è-ë */
        return 'e';
    case 0x8C: case 0x8D: case 0x8E: case 0x8F:                      /* Ì-Ï */
    case 0xAC: case 0xAD: case 0xAE: case 0xAF:                      /* ì-ï */
        return 'i';
    case 0x91: case 0xB1:                                            /* Ñ, ñ */
        return 'n';
    case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x98:/* Ò-Ø */
    case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB8:/* ò-ø */
        return 'o';
    case 0x99: case 0x9A: case 0x9B: case 0x9C:                      /* Ù-Ü */
    case 0xB9: case 0xBA: case 0xBB: case 0xBC:                      /* ù-ü */
        return 'u';
    case 0x9D: case 0xBD: case 0xBF:                                 /* Ý, ý, ÿ */
        return 'y';
    default:
        return 0;
    }
}

void acme_slug5(const char *label, char out[6]) {
    int j = 0;
    if (label) {
        const unsigned char *p = (const unsigned char *)label;
        while (*p && j < 5) {
            unsigned char c = *p;
            if (c >= 'a' && c <= 'z') {
                out[j++] = (char)c;
                p++;
            } else if (c >= 'A' && c <= 'Z') {
                out[j++] = (char)(c - 'A' + 'a');
                p++;
            } else if (c == 0xC3 && p[1]) {
                char folded = fold_latin1(p[1]);
                if (folded) out[j++] = folded;
                p += 2;
            } else if (c < 0x80) {
                p++;                       /* ASCII non-letter -- skip */
            } else if ((c & 0xE0) == 0xC0) {
                p += (p[1] ? 2 : 1);       /* skip 2-byte UTF-8 */
            } else if ((c & 0xF0) == 0xE0) {
                if (p[1] && p[2]) p += 3; else p++;
            } else if ((c & 0xF8) == 0xF0) {
                if (p[1] && p[2] && p[3]) p += 4; else p++;
            } else {
                p++;                       /* malformed -- skip 1 byte */
            }
        }
    }
    while (j < 5) out[j++] = 'x';
    out[5] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Checksum                                                          */
/* ------------------------------------------------------------------ */

char acme_check_char(const char *core) {
    int sum = 0;
    if (!core) return ALPH[0];
    for (int i = 0; core[i]; i++) {
        unsigned char c = (unsigned char)core[i];
        if (c == '_') continue;
        int v = char_value(c);
        if (v >= 0) sum += v;
    }
    return ALPH[(32 - (sum % 32)) % 32];
}

/* ------------------------------------------------------------------ */
/*  Mint                                                              */
/* ------------------------------------------------------------------ */

int acme_mint_id(char type,
                 const char *prefix,
                 const char *label,
                 int rand_len,
                 char *out,
                 size_t out_size) {
    if (!out || out_size == 0) return -1;
    out[0] = '\0';

    if (!isalpha((unsigned char)type)) return -1;
    type = (char)toupper((unsigned char)type);

    /* Normalize rand_len: <=0 -> 4; otherwise silently clamp to [2,8]. */
    if (rand_len <= 0) rand_len = 4;
    if (rand_len < 2)  rand_len = 2;
    if (rand_len > 8)  rand_len = 8;

    /* Days since epoch -- uint32 is good until year ~37000. */
    time_t now = time(NULL);
    uint32_t days = (uint32_t)((now - ACME_EPOCH) / 86400);

    char time_part[5];
    b32_encode(days, 4, time_part);

    char rand_part[16];
    if (randb32(rand_len, rand_part) != 0) return -1;

    int have_slug = (label && label[0]);
    char slug[6];
    if (have_slug) acme_slug5(label, slug);

    /* core = TYPE _ [SLUG] TIME RAND  (prefix is NOT part of core). */
    char core[32];
    int core_len;
    if (have_slug) {
        core_len = snprintf(core, sizeof(core), "%c_%s%s%s",
                            type, slug, time_part, rand_part);
    } else {
        core_len = snprintf(core, sizeof(core), "%c_%s%s",
                            type, time_part, rand_part);
    }
    if (core_len < 0 || (size_t)core_len >= sizeof(core)) return -1;

    char chk = acme_check_char(core);

    size_t prefix_len = (prefix && prefix[0]) ? strlen(prefix) : 0;
    size_t need = prefix_len + (size_t)core_len + 1u + 1u; /* +chk +NUL */
    if (need > out_size) {
        out[0] = '\0';
        return -1;
    }

    int n = (prefix_len > 0)
        ? snprintf(out, out_size, "%s%s%c", prefix, core, chk)
        : snprintf(out, out_size, "%s%c",          core, chk);
    if (n < 0 || (size_t)n >= out_size) {
        out[0] = '\0';
        return -1;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  Verify                                                            */
/* ------------------------------------------------------------------ */

int acme_verify_id(const char *id) {
    if (!id) return 0;

    /* Strip optional prefix: everything up to and including the last ':'. */
    const char *suffix = id;
    const char *colon = strrchr(id, ':');
    if (colon) suffix = colon + 1;

    size_t len = strlen(suffix);
    if (len < 9 || len > 20) return 0;   /* tightest envelope (see header) */

    /* TYPE _ */
    if (!(suffix[0] >= 'A' && suffix[0] <= 'Z')) return 0;
    if (suffix[1] != '_')                        return 0;

    /* SLUG presence inferred from position 2: a-z means a slug is present.
     * TIME/RAND/CHK never start with a lowercase letter (Crockford Base32
     * uses uppercase + digits only), so this is unambiguous. */
    int have_slug = (suffix[2] >= 'a' && suffix[2] <= 'z');
    size_t body_start = have_slug ? 7u : 2u;

    if (have_slug) {
        if (len < 7) return 0;
        for (size_t i = 2; i < 7; i++) {
            if (!(suffix[i] >= 'a' && suffix[i] <= 'z')) return 0;
        }
    }

    /* body = TIME(4) + RAND(2..8) + CHK(1)  =>  body_len in [7, 13]. */
    if (len < body_start + 7 || len > body_start + 13) return 0;
    for (size_t i = body_start; i < len; i++) {
        if (!strchr(ALPH, suffix[i])) return 0;
    }

    /* Checksum: sum of char_value over every non-underscore char. */
    int sum = 0;
    for (size_t i = 0; i < len; i++) {
        if (suffix[i] == '_') continue;
        int v = char_value((unsigned char)suffix[i]);
        if (v < 0) return 0;
        sum += v;
    }
    return (sum % 32) == 0;
}
