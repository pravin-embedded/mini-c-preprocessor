/*
 * Mini C Preprocessor
 * -------------------
 * A simplified C preprocessor written from scratch in C.
 *
 * The program reads a C source file character by character, removes
 * comments, stores object-like macros defined with #define, expands user
 * headers written as #include "header.h" and writes the result to an
 * output file.
 *
 * This is an educational implementation.  It is not a replacement for
 * the real preprocessor of GCC or Clang; see README.md for the full
 * list of supported features and for the known limitations.
 *
 * Build : gcc -Wall -Wextra -std=c11 preprocessor.c -o preprocessor
 * Usage : ./preprocessor input.c output.i
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sizes of the fixed buffers used throughout the program. */
#define MACRO_NAME_LEN    50
#define MACRO_VALUE_LEN   200
#define MAX_MACROS        256
#define MAX_LINE_LEN      1024
#define MAX_IDENT_LEN     256
#define MAX_PATH_LEN      512
#define MAX_WS_LEN        16
#define MAX_INCLUDE_DEPTH 16

/* One object-like macro: NAME is replaced by value. */
struct Macro {
    char name[MACRO_NAME_LEN];
    char value[MACRO_VALUE_LEN];
};

/* The macro table shared by the main file and every included header. */
struct MacroTable {
    struct Macro macros[MAX_MACROS];
    int count;
};

/* State that is carried along while the files are processed. */
struct Context {
    struct MacroTable table;
    long line; /* line number of the file currently being read */
};

/* ------------------------------------------------------------------ */
/* Macro table                                                         */
/* ------------------------------------------------------------------ */

static void macro_table_init(struct MacroTable *table)
{
    table->count = 0;
}

/* Returns the replacement text of a macro, or NULL when the name is
   not a known macro. */
static const char *macro_lookup(const struct MacroTable *table, const char *name)
{
    int i;

    for (i = 0; i < table->count; i++) {
        if (strcmp(table->macros[i].name, name) == 0) {
            return table->macros[i].value;
        }
    }
    return NULL;
}

/* Stores a macro.  A repeated #define replaces the previous value.
   Returns 0 when the macro cannot be stored. */
static int macro_define(struct MacroTable *table, const char *name, const char *value)
{
    int i;

    for (i = 0; i < table->count; i++) {
        if (strcmp(table->macros[i].name, name) == 0) {
            strcpy(table->macros[i].value, value);
            return 1;
        }
    }

    if (table->count >= MAX_MACROS) {
        fprintf(stderr, "error: macro table is full, '%s' not stored\n", name);
        return 0;
    }
    if (strlen(name) >= MACRO_NAME_LEN || strlen(value) >= MACRO_VALUE_LEN) {
        fprintf(stderr, "error: macro '%s' is too long to store\n", name);
        return 0;
    }

    strcpy(table->macros[table->count].name, name);
    strcpy(table->macros[table->count].value, value);
    table->count++;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Character, string and line helpers                                  */
/* ------------------------------------------------------------------ */

static int is_ident_start(int ch)
{
    return isalpha(ch) || ch == '_';
}

static int is_ident_char(int ch)
{
    return isalnum(ch) || ch == '_';
}

/* Horizontal white space only.  A newline is treated separately because
   it also ends a line. */
static int is_blank(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
}

/* Copies a string into a fixed buffer.  Returns 0 when it does not fit. */
static int copy_bounded(char *dest, size_t size, const char *src)
{
    size_t len = strlen(src);

    if (len >= size) {
        return 0;
    }
    memcpy(dest, src, len + 1);
    return 1;
}

/* Removes leading and trailing blanks by moving the start of the string
   and writing a terminator at the end. */
static char *trim(char *text)
{
    char *end;

    while (*text != '\0' && is_blank((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && is_blank((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

/* Reads one logical line into buf and consumes the newline that ends it.
   A line whose last character is a backslash is continued on the next
   physical line. */
static void read_logical_line(FILE *in, char *buf, size_t size)
{
    size_t len = 0;
    int ch;

    while ((ch = fgetc(in)) != EOF) {
        if (ch == '\n') {
            if (len > 0 && buf[len - 1] == '\\') {
                buf[--len] = '\0'; /* the line continues below */
                continue;
            }
            break;
        }
        if (len + 1 < size) {
            buf[len++] = (char)ch;
        }
    }
    buf[len] = '\0';
}

/* Reads the directive name that follows a '#'.  The character that ends
   the name is pushed back with ungetc() so that the caller can read the
   rest of the line. */
static int read_directive_name(FILE *in, char *name, size_t size)
{
    size_t len = 0;
    int ch;

    while ((ch = fgetc(in)) != EOF && is_blank(ch)) {
        /* skip the blanks between '#' and the name */
    }

    if (ch == EOF || ch == '\n' || !is_ident_start(ch)) {
        if (ch != EOF) {
            ungetc(ch, in);
        }
        name[0] = '\0';
        return 0;
    }

    name[len++] = (char)ch; /* the first character of the name */
    while ((ch = fgetc(in)) != EOF && is_ident_char(ch)) {
        if (len + 1 < size) {
            name[len++] = (char)ch;
        }
    }
    if (ch != EOF) {
        ungetc(ch, in);
    }
    name[len] = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* Header lookup                                                       */
/* ------------------------------------------------------------------ */

/* Writes the directory part of a path into dir.  A path without a slash
   gives ".". */
static void extract_dir(const char *path, char *dir, size_t size)
{
    const char *slash = strrchr(path, '/');
    size_t len;

    if (slash == NULL) {
        copy_bounded(dir, size, ".");
        return;
    }
    len = (size_t)(slash - path);
    if (len == 0) {
        len = 1; /* the file itself sits in "/" */
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
}

/* Opens a user header and writes the path that was used into opened.
   The directory of the including file is searched first, the current
   working directory second.  System headers written as <name> are never
   passed to this function. */
static FILE *open_include(const char *header, const char *including_file,
                          char *opened, size_t size)
{
    char dir[MAX_PATH_LEN];
    char path[MAX_PATH_LEN * 2];
    FILE *in;
    size_t dir_len;
    size_t header_len;

    if (header[0] == '/') {
        in = fopen(header, "r");
        if (in != NULL) {
            copy_bounded(opened, size, header);
        }
        return in;
    }

    extract_dir(including_file, dir, sizeof dir);
    dir_len = strlen(dir);
    header_len = strlen(header);
    if (dir_len + 1 + header_len + 1 > sizeof path) {
        fprintf(stderr, "error: path of header '%s' is too long\n", header);
        return NULL;
    }

    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, header, header_len + 1);

    in = fopen(path, "r");
    if (in != NULL) {
        copy_bounded(opened, size, path);
        return in;
    }

    in = fopen(header, "r");
    if (in != NULL) {
        copy_bounded(opened, size, header);
    }
    return in;
}

/* ------------------------------------------------------------------ */
/* Directives                                                          */
/* ------------------------------------------------------------------ */

static int process_stream(FILE *in, FILE *out, struct Context *ctx,
                          const char *path, int depth);

/* #define NAME [value]
   Only object-like macros are stored; a function-like macro such as
   #define ADD(a, b) is reported and ignored.  The directive itself is
   never written to the output. */
static int handle_define(FILE *in, struct Context *ctx, const char *path)
{
    char raw[MAX_LINE_LEN];
    char name[MACRO_NAME_LEN];
    char *text;
    char *value;
    size_t len = 0;

    read_logical_line(in, raw, sizeof raw);
    text = trim(raw);

    while (text[len] != '\0' && is_ident_char((unsigned char)text[len])) {
        len++;
    }
    if (len == 0) {
        fprintf(stderr, "%s:%ld: error: #define without a macro name\n",
                path, ctx->line);
        return 0;
    }
    if (len >= sizeof name) {
        fprintf(stderr, "%s:%ld: error: macro name is too long\n",
                path, ctx->line);
        return 0;
    }
    memcpy(name, text, len);
    name[len] = '\0';

    value = trim(text + len);

    if (*value == '(') {
        fprintf(stderr,
                "%s:%ld: warning: function-like macro '%s' is not supported\n",
                path, ctx->line, name);
        return 1;
    }
    if (*value == '\0') {
        value = "1"; /* the C standard gives an empty list the value 1 */
    }
    if (strlen(value) >= MACRO_VALUE_LEN) {
        fprintf(stderr, "%s:%ld: error: value of macro '%s' is too long\n",
                path, ctx->line, name);
        return 0;
    }

    return macro_define(&ctx->table, name, value);
}

/* #include "header.h"
   The header is searched next to the including file and is then
   processed recursively with the same macro table, so a macro defined in
   a header stays available in the rest of the source file. */
static int handle_include(FILE *in, FILE *out, struct Context *ctx,
                          const char *path, int depth)
{
    char raw[MAX_LINE_LEN];
    char header[MAX_LINE_LEN];
    char opened[MAX_PATH_LEN * 2];
    char *text;
    char *end;
    FILE *included;
    long saved_line;
    int result;

    read_logical_line(in, raw, sizeof raw);
    text = trim(raw);

    if (*text != '"') {
        fprintf(stderr,
                "%s:%ld: warning: system headers are not supported, #include ignored\n",
                path, ctx->line);
        return 1;
    }
    end = strchr(text + 1, '"');
    if (end == NULL) {
        fprintf(stderr, "%s:%ld: error: header name is not closed\n",
                path, ctx->line);
        return 0;
    }
    *end = '\0';
    if (!copy_bounded(header, sizeof header, text + 1)) {
        fprintf(stderr, "%s:%ld: error: header name is too long\n",
                path, ctx->line);
        return 0;
    }

    if (depth >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr,
                "%s:%ld: error: headers are nested deeper than %d levels\n",
                path, ctx->line, MAX_INCLUDE_DEPTH);
        return 0;
    }

    included = open_include(header, path, opened, sizeof opened);
    if (included == NULL) {
        fprintf(stderr, "error: cannot open header '%s' (included from %s)\n",
                header, path);
        return 0;
    }

    saved_line = ctx->line;
    ctx->line = 1; /* the line numbers of a header start again at 1 */
    result = process_stream(included, out, ctx, opened, depth + 1);
    ctx->line = saved_line;
    fclose(included);

    return result;
}

/* Reads the directive after a '#' and dispatches it.  Returns 0 on a
   fatal error. */
static int handle_directive(FILE *in, FILE *out, struct Context *ctx,
                            const char *path, int depth)
{
    char name[32];
    char rest[MAX_LINE_LEN];

    if (!read_directive_name(in, name, sizeof name)) {
        /* A '#' that starts no name, a line marker for example. */
        read_logical_line(in, rest, sizeof rest);
        return 1;
    }

    if (strcmp(name, "define") == 0) {
        return handle_define(in, ctx, path);
    }
    if (strcmp(name, "include") == 0) {
        return handle_include(in, out, ctx, path, depth);
    }

    fprintf(stderr, "%s:%ld: warning: #%s is not supported and was ignored\n",
            path, ctx->line, name);
    read_logical_line(in, rest, sizeof rest);
    return 1;
}
/* ------------------------------------------------------------------ */
/* The scanner                                                         */
/* ------------------------------------------------------------------ */

/* Writes text to the output and remembers the last character written. */
static void emit_text(const char *text, FILE *out, int *last_out)
{
    size_t len = strlen(text);

    if (len == 0) {
        return;
    }
    fputs(text, out);
    *last_out = (unsigned char)text[len - 1];
}

/* A removed comment still separates two tokens in C, so one blank is
   inserted when dropping the comment would join its neighbours. */
static void close_block_comment(FILE *in, FILE *out, int *last_out)
{
    int next = fgetc(in);

    if (next == EOF) {
        return;
    }
    ungetc(next, in);

    if (!is_blank(next) && *last_out != '\n' && !is_blank(*last_out)) {
        fputc(' ', out);
        *last_out = ' ';
    }
}

/* Copies a string literal, escaped characters included, to the output.
   The quotes are consumed, so a comment marker inside a string is kept. */
static void emit_string(FILE *in, FILE *out, int *last_out)
{
    int ch;

    fputc('"', out);
    *last_out = '"';

    while ((ch = fgetc(in)) != EOF && ch != '\n') {
        fputc(ch, out);
        *last_out = ch;

        if (ch == '\\') {
            ch = fgetc(in); /* the escaped character is copied as it is */
            if (ch == EOF) {
                break;
            }
            fputc(ch, out);
            *last_out = ch;
        } else if (ch == '"') {
            break;
        }
    }

    if (ch == '\n') { /* the string was not closed on this line */
        fputc('\n', out);
        *last_out = '\n';
    }
}

/* Copies an identifier to the output, or the replacement text when the
   name is a known macro. */
static void emit_identifier(FILE *in, FILE *out, struct Context *ctx,
                            int first, int *last_out)
{
    const char *replacement;
    char ident[MAX_IDENT_LEN];
    size_t len = 0;
    int ch = first;
    int too_long = 0;

    ident[len++] = (char)ch;
    while ((ch = fgetc(in)) != EOF && is_ident_char(ch)) {
        if (len + 1 < sizeof ident) {
            ident[len++] = (char)ch;
        } else {
            too_long = 1;
        }
    }
    if (ch != EOF) {
        ungetc(ch, in); /* the delimiter has to be read again */
    }
    ident[len] = '\0';

    if (too_long) {
        emit_text(ident, out, last_out);
        return;
    }

    replacement = macro_lookup(&ctx->table, ident);
    emit_text(replacement != NULL ? replacement : ident, out, last_out);
}
/* Reads one file character by character and writes the preprocessed
   text to out.  Returns 0 on a fatal error. */
static int process_stream(FILE *in, FILE *out, struct Context *ctx,
                          const char *path, int depth)
{
    char pending[MAX_WS_LEN];
    int pending_len = 0;
    int pending_extra = 0;
    int at_line_start = 1;
    int in_block_comment = 0;
    int last_out = '\n';
    int ch;

    while ((ch = fgetc(in)) != EOF) {
        if (ch == '\n') {
            ctx->line++;
        }

        if (in_block_comment) {
            if (ch == '*') {
                int next = fgetc(in);
                if (next == '/') {
                    in_block_comment = 0;
                    close_block_comment(in, out, &last_out);
                } else if (next != EOF) {
                    ungetc(next, in);
                }
            }
            continue;
        }

        /* Blanks in front of a possible directive are held back, so that
           a whole directive line can be left out of the output. */
        if (at_line_start && is_blank(ch)) {
            if (pending_len < MAX_WS_LEN) {
                pending[pending_len++] = (char)ch;
            } else {
                pending_extra++;
            }
            continue;
        }

        if (at_line_start && ch == '#') {
            pending_len = 0;
            pending_extra = 0;
            if (!handle_directive(in, out, ctx, path, depth)) {
                return 0;
            }
            continue; /* at_line_start stays 1: the line was consumed */
        }

        if (pending_len > 0 || pending_extra > 0) {
            int i;
            for (i = 0; i < pending_len; i++) {
                fputc(pending[i], out);
            }
            for (i = 0; i < pending_extra; i++) {
                fputc(' ', out);
            }
            pending_len = 0;
            pending_extra = 0;
            last_out = ' ';
        }

        if (ch == '\n') {
            fputc('\n', out);
            last_out = '\n';
            at_line_start = 1;
            continue;
        }

        at_line_start = 0;

        if (ch == '/') {
            int next = fgetc(in);

            if (next == '/') { /* a comment up to the end of the line */
                while ((next = fgetc(in)) != EOF && next != '\n') {
                    /* the body of the comment is dropped */
                }
                if (next == '\n') {
                    ctx->line++;
                    fputc('\n', out);
                    last_out = '\n';
                    at_line_start = 1;
                }
                continue;
            }
            if (next == '*') {
                in_block_comment = 1;
                continue;
            }
            if (next != EOF) {
                ungetc(next, in); /* it is only a division */
            }
            fputc('/', out);
            last_out = '/';
            continue;
        }

        if (ch == '"') {
            emit_string(in, out, &last_out);
            continue;
        }

        if (is_ident_start(ch)) {
            emit_identifier(in, out, ctx, ch, &last_out);
            continue;
        }

        fputc(ch, out);
        last_out = ch;
    }

    if (in_block_comment) {
        fprintf(stderr, "%s:%ld: error: the file ends inside a block comment\n",
                path, ctx->line);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    struct Context ctx;
    FILE *in;
    FILE *out;
    int ok;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.c> <output.i>\n", argv[0]);
        return EXIT_FAILURE;
    }

    in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "error: cannot open input file '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    out = fopen(argv[2], "w");
    if (out == NULL) {
        fprintf(stderr, "error: cannot create output file '%s'\n", argv[2]);
        fclose(in);
        return EXIT_FAILURE;
    }

    macro_table_init(&ctx.table);
    ctx.line = 1;

    ok = process_stream(in, out, &ctx, argv[1], 0);

    fclose(in);
    if (fclose(out) != 0) {
        fprintf(stderr, "error: could not finish writing '%s'\n", argv[2]);
        ok = 0;
    }

    if (!ok) {
        fprintf(stderr, "error: '%s' was not preprocessed completely\n", argv[1]);
        return EXIT_FAILURE;
    }

    printf("%s -> %s\n", argv[1], argv[2]);
    return EXIT_SUCCESS;
}
