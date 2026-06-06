/*
 * cli/main.c -- acmeid command-line front-end.
 *
 * Subcommands:
 *   acmeid mint   -t TYPE [-p PREFIX] [-l LABEL] [-n LEN]
 *   acmeid verify [-v] ID
 *   acmeid batch  -t TYPE [-p PREFIX] [-n LEN]      (labels on stdin)
 *   acmeid help | version
 *
 * Discipline:
 *   - mint writes ID + '\n' to stdout, all diagnostics to stderr.
 *   - verify exits 0 if valid, 1 if invalid; -v prints "ID: VALID/INVALID".
 *   - batch reads one label per line, emits "label\tid\n"; empty
 *     lines become "\t<id-without-slug>\n".  Line-buffered output.
 */

#include "acmeid.h"

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ACMEID_VERSION
#  define ACMEID_VERSION "0.1.0"
#endif

static void print_usage(FILE *fp) {
    fputs(
        "usage:\n"
        "  acmeid mint   -t TYPE [-p PREFIX] [-l LABEL] [-n LEN]\n"
        "  acmeid verify [-v] ID\n"
        "  acmeid batch  -t TYPE [-p PREFIX] [-n LEN]   (labels on stdin)\n"
        "  acmeid help | version\n"
        "\n"
        "options:\n"
        "  -t, --type TYPE     single ASCII letter (required for mint/batch)\n"
        "  -p, --prefix PFX    optional CURIE-style prefix, e.g. 'lepus:'\n"
        "  -l, --label LBL     optional label for the 5-letter slug\n"
        "  -n, --length N      random width, clamped to [2,8] (default 4)\n"
        "  -v, --verbose       print VALID/INVALID for verify\n"
        "  -h, --help          show this help\n"
        "\n",
        fp);
}

/* ------------------------------------------------------------------ */
/*  Subcommands                                                       */
/* ------------------------------------------------------------------ */

static int do_mint(int argc, char **argv) {
    static struct option opts[] = {
        {"type",   required_argument, 0, 't'},
        {"prefix", required_argument, 0, 'p'},
        {"label",  required_argument, 0, 'l'},
        {"length", required_argument, 0, 'n'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    char type = 0;
    const char *prefix = NULL;
    const char *label  = NULL;
    int rand_len = 4;
    int c;
    while ((c = getopt_long(argc, argv, "t:p:l:n:h", opts, NULL)) != -1) {
        switch (c) {
        case 't': type = optarg[0]; break;
        case 'p': prefix = optarg;  break;
        case 'l': label = optarg;   break;
        case 'n': rand_len = atoi(optarg); break;
        case 'h': print_usage(stdout); return 0;
        default:  return 2;
        }
    }
    if (!type) {
        fprintf(stderr, "acmeid mint: -t TYPE is required\n");
        return 2;
    }
    char id[ACME_MAX_ID_LEN];
    int n = acme_mint_id(type, prefix, label, rand_len, id, sizeof(id));
    if (n < 0) {
        fprintf(stderr, "acmeid mint: failed to mint (bad type or buffer)\n");
        return 2;
    }
    puts(id);
    return 0;
}

static int do_verify(int argc, char **argv) {
    static struct option opts[] = {
        {"verbose", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int verbose = 0;
    int c;
    while ((c = getopt_long(argc, argv, "vh", opts, NULL)) != -1) {
        switch (c) {
        case 'v': verbose = 1; break;
        case 'h': print_usage(stdout); return 0;
        default:  return 2;
        }
    }
    /* Allow piping: if no positional, read one line from stdin. */
    char buf[ACME_MAX_ID_LEN * 2];
    const char *id = NULL;
    if (optind < argc) {
        id = argv[optind];
    } else if (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
        id = buf;
    } else {
        fprintf(stderr, "acmeid verify: ID argument or stdin required\n");
        return 2;
    }
    int valid = acme_verify_id(id);
    if (verbose) {
        printf("%s: %s\n", id, valid ? "VALID" : "INVALID");
    }
    return valid ? 0 : 1;
}

static int do_batch(int argc, char **argv) {
    static struct option opts[] = {
        {"type",   required_argument, 0, 't'},
        {"prefix", required_argument, 0, 'p'},
        {"length", required_argument, 0, 'n'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    char type = 0;
    const char *prefix = NULL;
    int rand_len = 4;
    int c;
    while ((c = getopt_long(argc, argv, "t:p:n:h", opts, NULL)) != -1) {
        switch (c) {
        case 't': type = optarg[0]; break;
        case 'p': prefix = optarg;  break;
        case 'n': rand_len = atoi(optarg); break;
        case 'h': print_usage(stdout); return 0;
        default:  return 2;
        }
    }
    if (!type) {
        fprintf(stderr, "acmeid batch: -t TYPE is required\n");
        return 2;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        const char *label = (len > 0) ? line : NULL;
        char id[ACME_MAX_ID_LEN];
        int n = acme_mint_id(type, prefix, label, rand_len, id, sizeof(id));
        if (n < 0) {
            fprintf(stderr, "acmeid batch: failed to mint for '%s'\n", line);
            return 2;
        }
        printf("%s\t%s\n", line, id);
    }
    return ferror(stdin) ? 2 : 0;
}

/* ------------------------------------------------------------------ */
/*  Dispatch                                                          */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "help") || !strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
        print_usage(stdout);
        return 0;
    }
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version")) {
        printf("acmeid %s\n", ACMEID_VERSION);
        return 0;
    }
    if (!strcmp(cmd, "mint"))   return do_mint  (argc - 1, argv + 1);
    if (!strcmp(cmd, "verify")) return do_verify(argc - 1, argv + 1);
    if (!strcmp(cmd, "batch"))  return do_batch (argc - 1, argv + 1);

    fprintf(stderr, "acmeid: unknown command '%s' (try 'acmeid help')\n", cmd);
    return 2;
}
