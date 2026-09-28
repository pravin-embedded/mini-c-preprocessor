/*
 * Shows how comments are removed while string literals are kept.
 * Run: ./preprocessor examples/comments_and_strings.c output/comments.i
 */

#define GREETING "hello"
#define SLASH    '/'

// A line comment is removed completely.
int result = 10 / 2;          /* a block comment is removed too */

char *text = "not a /* comment */ here";
char *quote = "an escaped quote \" stays";
char *end_of_line = "a string is never cut // here";

/* A block comment
   spread over
   several lines. */
int still_here = GREETING[0];
