#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_MACROS 20
#define MAX_NAME 50
#define MAX_VALUE 200
#define MAX_PATH 200

struct Macro
{
    char name[MAX_NAME];
    char value[MAX_VALUE];
};

/* Find a macro in the macro table */
int find_macro(struct Macro macros[], int count, char name[])
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (strcmp(macros[i].name, name) == 0)
            return i;
    }

    return -1;
}

/* Copy one file into another */
void copy_file(FILE *source, FILE *output)
{
    int ch;

    while ((ch = fgetc(source)) != EOF)
    {
        fputc(ch, output);
    }
}

/* Process a user header file */
void process_file(FILE *source,
                  FILE *output,
                  struct Macro macros[],
                  int *macro_count)
{
    int ch;
    int next;

    int in_string = 0;
    int escaped = 0;

    while ((ch = fgetc(source)) != EOF)
    {
        /*
         * Handle preprocessor directives
         */
        if (ch == '#')
        {
            char directive[300];
            int length = 0;

            next = fgetc(source);

            while (next != EOF && next != '\n')
            {
                if (length < 299)
                    directive[length++] = next;

                next = fgetc(source);
            }

            directive[length] = '\0';

            /*
             * Skip spaces after #
             */
            int pos = 0;

            while (directive[pos] == ' ' ||
                   directive[pos] == '\t')
            {
                pos++;
            }

            /*
             * Check for #define
             */
            if (strncmp(&directive[pos], "define", 6) == 0 &&
                (directive[pos + 6] == ' ' ||
                 directive[pos + 6] == '\t'))
            {
                pos += 6;

                while (directive[pos] == ' ' ||
                       directive[pos] == '\t')
                {
                    pos++;
                }

                /*
                 * Read macro name
                 */
                char name[MAX_NAME];
                int i = 0;

                while (directive[pos] != '\0' &&
                       directive[pos] != ' ' &&
                       directive[pos] != '\t')
                {
                    if (i < MAX_NAME - 1)
                        name[i++] = directive[pos];

                    pos++;
                }

                name[i] = '\0';

                /*
                 * Skip spaces before value
                 */
                while (directive[pos] == ' ' ||
                       directive[pos] == '\t')
                {
                    pos++;
                }

                /*
                 * Store macro value
                 */
                char value[MAX_VALUE];
                i = 0;

                while (directive[pos] != '\0')
                {
                    if (i < MAX_VALUE - 1)
                        value[i++] = directive[pos];

                    pos++;
                }

                value[i] = '\0';

                /*
                 * Add macro to table
                 */
                if (name[0] != '\0' &&
                    *macro_count < MAX_MACROS)
                {
                    strcpy(macros[*macro_count].name, name);
                    strcpy(macros[*macro_count].value, value);

                    (*macro_count)++;
                }

                /*
                 * #define should not appear in output
                 */
                continue;
            }

            /*
             * Check for #include "file.h"
             */
            if (strncmp(&directive[pos], "include", 7) == 0 &&
                (directive[pos + 7] == ' ' ||
                 directive[pos + 7] == '\t'))
            {
                pos += 7;

                while (directive[pos] == ' ' ||
                       directive[pos] == '\t')
                {
                    pos++;
                }

                /*
                 * We support only:
                 *
                 * #include "file.h"
                 */
                if (directive[pos] == '"')
                {
                    char filename[MAX_PATH];
                    int i = 0;

                    pos++;

                    while (directive[pos] != '\0' &&
                           directive[pos] != '"')
                    {
                        if (i < MAX_PATH - 1)
                            filename[i++] = directive[pos];

                        pos++;
                    }

                    filename[i] = '\0';

                    /*
                     * Open the user header
                     */
                    FILE *header = fopen(filename, "r");

                    if (header != NULL)
                    {
                        /*
                         * Process header using the
                         * same macro table.
                         */
                        process_file(header,
                                     output,
                                     macros,
                                     macro_count);

                        fclose(header);
                    }

                    continue;
                }
            }

            /*
             * If it was not a #define or #include,
             * preserve the directive.
             */
            fputc('#', output);
            fputs(directive, output);

            if (next == '\n')
                fputc('\n', output);

            continue;
        }

        /*
         * Handle strings
         */
        if (ch == '"' && !escaped)
        {
            in_string = !in_string;
        }

        if (in_string)
        {
            fputc(ch, output);

            if (ch == '\\' && !escaped)
                escaped = 1;
            else
                escaped = 0;

            continue;
        }

        /*
         * Handle comments
         */
        if (ch == '/')
        {
            next = fgetc(source);

            /*
             * Single-line comment
             */
            if (next == '/')
            {
                while ((ch = fgetc(source)) != EOF &&
                       ch != '\n')
                {
                    /* skip comment */
                }

                if (ch == '\n')
                    fputc('\n', output);

                continue;
            }

            /*
             * Multi-line comment
             */
            else if (next == '*')
            {
                while ((ch = fgetc(source)) != EOF)
                {
                    if (ch == '*')
                    {
                        next = fgetc(source);

                        if (next == '/')
                            break;

                        if (next != EOF)
                            ungetc(next, source);
                    }
                }

                continue;
            }

            /*
             * Normal '/'
             */
            else
            {
                if (next != EOF)
                    ungetc(next, source);
            }
        }

        /*
         * Handle identifiers and macro replacement
         */
        if (isalpha((unsigned char)ch) || ch == '_')
        {
            char word[MAX_NAME];
            int i = 0;

            do
            {
                if (i < MAX_NAME - 1)
                    word[i++] = ch;

                ch = fgetc(source);

            } while (ch != EOF &&
                     (isalnum((unsigned char)ch) ||
                      ch == '_'));

            word[i] = '\0';

            /*
             * Check whether this identifier
             * is a macro.
             */
            int index = find_macro(macros,
                                    *macro_count,
                                    word);

            if (index != -1)
            {
                fputs(macros[index].value, output);
            }
            else
            {
                fputs(word, output);
            }

            /*
             * Put the character after the
             * identifier back into the file.
             */
            if (ch != EOF)
                ungetc(ch, source);

            continue;
        }

        /*
         * Normal character
         */
        fputc(ch, output);
    }
}

int main(int argc, char *argv[])
{
    FILE *input;
    FILE *output;

    struct Macro macros[MAX_MACROS];
    int macro_count = 0;

    /*
     * Program usage:
     *
     * ./preprocessor input.c output.i
     */
    if (argc != 3)
    {
        printf("Usage: %s input.c output.i\n", argv[0]);
        return 1;
    }

    /*
     * Open input file
     */
    input = fopen(argv[1], "r");

    if (input == NULL)
    {
        printf("Error: Cannot open input file\n");
        return 1;
    }

    /*
     * Open output file
     */
    output = fopen(argv[2], "w");

    if (output == NULL)
    {
        printf("Error: Cannot create output file\n");

        fclose(input);

        return 1;
    }

    /*
     * Process complete input file
     */
    process_file(input,
                 output,
                 macros,
                 &macro_count);

    fclose(input);
    fclose(output);

    printf("Preprocessing completed successfully.\n");

    return 0;
}
