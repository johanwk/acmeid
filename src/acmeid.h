/*
 * acmeid.h -- Public API for ACME Localname ID minting and verification.
 *
 * The format (v1.1) is:
 *
 *     [PREFIX:] TYPE _ [SLUG5] TIME4 RAND_N CHK1
 *
 *   PREFIX   optional CURIE-style prefix (NOT part of the checksum)
 *   TYPE     single uppercase ASCII letter, domain-owned
 *   _        literal underscore
 *   SLUG5    optional 5 lowercase ASCII letters derived from a label
 *   TIME4    days since 2020-01-01 UTC, Crockford Base32, 4 chars
 *   RAND_N   N random Crockford Base32 chars, N in [2,8] (default 4)
 *   CHK1     check character such that sum(values) mod 32 == 0
 *
 * All functions in this header are reentrant: they keep no global
 * state beyond the immutable alphabet constants.  The only
 * platform-conditional code lives in acme_random_bytes().
 */

#ifndef ACMEID_H
#define ACMEID_H

#include <stddef.h>

#if defined(_WIN32) && defined(ACMEID_BUILDING_DLL)
#  define ACME_API __declspec(dllexport)
#else
#  define ACME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Crockford Base32 alphabet (no I, L, O, U). */
#define ACME_ALPH        "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

/* Epoch: 2020-01-01 00:00:00 UTC, in seconds since the Unix epoch. */
#define ACME_EPOCH       1577836800

/* Generous upper bound for prefix + ID + NUL. */
#define ACME_MAX_ID_LEN  64

/*
 * Mint a new ACME localname ID.
 *
 *   type      Single ASCII letter (case-insensitive); upper-cased
 *             internally.  Returns -1 if !isalpha(type).
 *   prefix    Optional CURIE-style prefix (e.g. "lepus:"). NULL or
 *             "" omits it.  Copied verbatim; NOT part of the checksum.
 *   label     Optional human label used to derive a 5-letter slug.
 *             NULL or "" omits the slug entirely.
 *   rand_len  Random-section width in Base32 chars.  Values <= 0
 *             default to 4; other values are silently clamped to [2,8].
 *   out       Caller-provided output buffer.
 *   out_size  Capacity of `out`, including the terminating NUL.
 *
 * Returns the length of the written ID (excluding NUL) on success, or
 * -1 on error (bad type, NULL/zero-size buffer, RNG failure, or
 * buffer too small).  On error, out[0] is set to '\0' when possible.
 */
ACME_API int  acme_mint_id(char type,
                           const char *prefix,
                           const char *label,
                           int rand_len,
                           char *out,
                           size_t out_size);

/*
 * Verify an ACME ID by checksum.  Tolerates an optional CURIE-style
 * prefix terminated by ':' (the prefix is ignored, not validated).
 * Returns 1 if the ID is well-formed and the checksum is valid; 0
 * otherwise.  NULL input returns 0.
 */
ACME_API int  acme_verify_id(const char *id);

/*
 * Derive a 5-character lowercase slug from a label.  `out` must point
 * to a buffer of at least 6 bytes.  ASCII A-Z is folded to a-z.  A
 * small set of Latin-1 supplement letters (UTF-8 0xC3-prefixed) is
 * folded to their plain ASCII counterparts (so "Ångström" -> "angst").
 * Any other characters are skipped.  If fewer than 5 letters remain,
 * the slug is padded with 'x'.
 */
ACME_API void acme_slug5(const char *label, char out[6]);

/*
 * Compute the check character for a core string of the form
 *   TYPE _ [SLUG5] TIME4 RAND_N
 */
ACME_API char acme_check_char(const char *core);

/*
 * Fill `buf` with `n` cryptographically random bytes.
 * Returns 0 on success, -1 on error.
 */
ACME_API int  acme_random_bytes(void *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* ACMEID_H */
