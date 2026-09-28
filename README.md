# Mini C Preprocessor in C

## Overview

This project implements a simplified C preprocessor in C. Its purpose is to
understand how a preprocessing stage works internally: how a source file is read,
how comments and directives are recognised, and how a macro table is built and
applied to the text.

This is an educational implementation. It is **not** a complete replacement for the
preprocessor of GCC or Clang, and it does not follow the full C preprocessing
standard.

The program takes an input `.c` file and an output `.i` file as command-line
arguments:

```bash
./preprocessor input.c output.i
```

## Features

The following features are implemented:

* Command-line handling of the input and output file names
* Removal of `//` single-line comments
* Removal of `/* ... */` multi-line comments
* Preservation of `//` and `/* ... */` when they appear inside string literals
* Basic escaped-quote handling inside strings
* Detection and storage of object-like macros with `#define`
* Macro replacement when an identifier matches a stored macro
* Expansion of user-defined header files with:

  ```c
  #include "header.h"
  ```
* Recursive processing of the included user header files
* One shared macro table for the main source file and all included headers
* Generation of a preprocessed `.i` output file
* Warnings on `stderr` for directives that are not supported, and a non-zero exit
  status for fatal errors

## How It Works

The processing pipeline is a single character-by-character scan of the input file:

```text
Input .c file
      |
      v
Read character by character
      |
      +----> Comments ------> Remove
      |
      +----> Strings -------> Preserve
      |
      +----> #define -----> Store macro
      |
      +----> #include -----> Process user header
      |
      +----> Identifier ----> Check macro table
      |                         |
      |                         +--> Replace if found
      |
      v
Output .i file
```

The scan keeps a small amount of state: whether it is at the start of a line, and
whether it is inside a block comment or inside a string literal. A `#` that is
found at the start of a line is treated as a directive and the rest of that line
is handed to a directive handler.

Macros are stored in an array of structures:

```c
struct Macro
{
    char name[50];
    char value[200];
};
```

`#define NAME value` is parsed into this structure and stored in a table of
`struct Macro` entries. When the scanner later reads an identifier, it looks the
name up in that table and writes the stored value instead of the name when a
match is found.

The same macro table is passed down while a header is processed, so a macro
defined in a header is still available in the rest of the main source file. The
header file is opened and processed recursively by the same scanning function,
which is why included headers are expanded at the position of the `#include`.

## Example

### main.c

```c
#include "myheader.h"

int main()
{
    int x = VALUE;
    return 0;
}
```

### myheader.h

```c
#define VALUE 100

int header_var = 50;
```

### Generated output.i

```c

int header_var = 50;

int main()
{
    int x = 100;
    return 0;
}
```

The generated file is produced with:

```bash
./preprocessor examples/main.c output/output.i
```

What happened:

* `#include "myheader.h"` was removed and the content of `myheader.h` was written
  in its place, which is where `int header_var = 50;` comes from. The empty first
  line of the output is the blank line that follows `#define VALUE 100` in the
  header; blank lines of the input are kept as they are.
* `#define VALUE 100` was stored in the macro table and was not written to the
  output, because a directive line never reaches the output.
* The identifier `VALUE` in `int x = VALUE;` was found in the macro table and was
  replaced by its value, `100`.
* `int main()`, the braces and `return 0;` are copied unchanged.

## Compilation

```bash
gcc preprocessor.c -o preprocessor
```

## Usage

```bash
./preprocessor input.c output.i
```

* `input.c` is the source file to preprocess
* `output.i` is the preprocessed file that is generated

The program prints a short message when the file was processed and returns a
non-zero status if the file could not be read, if a header could not be found or
if a fatal error was found while scanning.

## Project Structure

```text
mini-c-preprocessor/
│
├── preprocessor.c
├── README.md
├── .gitignore
├── examples/
│   ├── main.c
│   ├── myheader.h
│   └── comments_and_strings.c
└── output/
    └── output.i
```

* `preprocessor.c` — the whole program: macro table, directive handling and the
  scanner
* `examples/main.c` and `examples/myheader.h` — the example used above
* `examples/comments_and_strings.c` — shows comment removal and the preservation
  of comment markers and escaped quotes inside string literals
* `output/output.i` — the preprocessed result of `examples/main.c`

## Limitations

This is a simplified, educational implementation. It does not aim to support the
full C preprocessing standard, and it is not intended to replace the real
preprocessor of a compiler.

* Only object-like `#define` macros are supported
* Function-like macros are not implemented; they are reported and ignored
* System headers written as `<...>` are not implemented
* Conditional compilation with `#if`, `#ifdef`, `#ifndef` and `#endif` is not
  implemented; such directives are reported and their content is still copied to
  the output
* The include search rules of GCC are not implemented; a header is only looked
  for next to the including file and in the current working directory
* Include guards are not implemented, so a header that includes itself stops at
  the nesting limit instead of being skipped
* A macro value is inserted as it was written; the inserted text is not scanned
  again, so nested macro expansion is not performed
* Comments inside a macro value are stored as part of the value
* `#`, `##`, `__FILE__`, `__LINE__` and the other predefined macros are not
  implemented
* Character literals such as `'/'` are not tracked, and a file that mixes `'` and
  `"` may be scanned incorrectly
* The macro table and all buffers have fixed sizes and are limited to a few
  hundred macros and lines

## Concepts Practiced

* File handling in C
* Command-line arguments
* Structures and arrays of structures
* String handling
* Character-by-character parsing
* `fgetc()` / `fputc()`
* `fopen()` / `fclose()`
* `ungetc()` to push a delimiter back into the stream
* Recursive function calls
* Macro lookup with `strcmp()`
* Basic lexical-style parsing
* State tracking for strings, escaped characters and block comments
* Fixed-size buffers with bounds checks, where dynamic memory was not needed
* Error reporting and return codes

## Learning Objective

The main purpose of this project was to understand how a preprocessing stage can
be implemented with basic C concepts, instead of using an existing preprocessing
library or a ready-made tool. The program shows the steps a real preprocessor
performs — scanning, collecting directives, keeping a macro table and writing a
new file — with nothing more than standard C file and string functions.

## Future Improvements

Possible next steps:

* Function-like macros with argument substitution
* System header support and an include search path
* `#ifdef` and `#ifndef` together with include guards
* Conditional compilation with `#if` and `#else`
* Nested and recursive macro expansion
* A preprocessor for the output, such as `#line` markers
* Better error handling, including the column and the include stack in messages
* Splitting the code into separate source files for the parser, the macro table
  and the file processing
* Automated tests for the scanner and the macro table

## Author

Developed as a C programming and systems-level learning project.
