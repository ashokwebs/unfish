#include "uf_learn.h"
#include "../runtime/uf_runtime.h"
#include "../interpreter/uf_interpreter.h"
#include "../lexer/uf_lexer.h"
#include "../parser/uf_parser.h"
#include "../semantic/uf_semantic.h"
#include "../common/uf_arena.h"
#include "../common/uf_diagnostic.h"
#include "../common/uf_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int id;
    const char* title;
    const char* description;
    const char* example;
    const char* challenge;
    const char* expected_output;
    const char* hint;
    const char* solution;
} Lesson;

static const Lesson LESSONS[] = {
    {
        1,
        "Variables and Output",
        "In Unfish, output is printed using the 'say' keyword.\n"
        "Variables are declared using 'let', e.g. 'let name = \"Alice\"'.\n"
        "Unfish is dynamically typed with indentation-based blocks.",
        "let greeting = \"Hello, Unfish!\"\n"
        "say greeting",
        "Declare a variable named 'score' with the value 100, then print it using 'say'.",
        "100\n",
        "Use 'let score = 100' followed by 'say score'.",
        "let score = 100\nsay score"
    },
    {
        2,
        "Arithmetic and Logic",
        "Unfish supports arithmetic operators: +, -, *, /, %.\n"
        "Logical operators are words: 'and', 'or', 'not'.\n"
        "Comparisons: ==, !=, <, <=, >, >=.",
        "say (10 + 5) * 2\n"
        "say true and not false",
        "Compute and print (50 * 2) - 15 using 'say'.",
        "85\n",
        "Write: say (50 * 2) - 15",
        "say (50 * 2) - 15"
    },
    {
        3,
        "Control Flow: If & Loops",
        "Conditionals use 'if <condition>:' followed by an indented block.\n"
        "Optional 'else:' or 'else if:' can follow.\n"
        "Unfish has 'repeat <n> times:' and 'while <cond>:' loops.",
        "let x = 10\n"
        "if x > 5:\n"
        "    say \"Big\"\n"
        "else:\n"
        "    say \"Small\"",
        "Write an if-statement checking if 42 > 20, and if true, print \"valid\".",
        "valid\n",
        "if 42 > 20:\n    say \"valid\"",
        "if 42 > 20:\n    say \"valid\""
    },
    {
        4,
        "Functions & Parameters",
        "Functions are declared with 'function <name>(<params>):' or 'fn'.\n"
        "Parameters can have default values: 'fn greet(name=\"World\"):'.\n"
        "Use 'return' to return values.",
        "function double(n):\n"
        "    return n * 2\n"
        "\n"
        "say double(21)",
        "Define a function 'cube(n)' that returns n * n * n, then call 'say cube(3)'.",
        "27\n",
        "function cube(n):\n    return n * n * n\nsay cube(3)",
        "function cube(n):\n    return n * n * n\nsay cube(3)"
    },
    {
        5,
        "Collections: Arrays and Maps",
        "Arrays are written with square brackets: [1, 2, 3].\n"
        "Maps use key-value pairs: {\"name\": \"Alice\", \"age\": 30}.\n"
        "Index elements with arr[0] or map[\"key\"] or map.key.",
        "let fruits = [\"apple\", \"banana\"]\n"
        "say fruits[0]",
        "Create an array with numbers [10, 20, 30] and print the second element (index 1).",
        "20\n",
        "let arr = [10, 20, 30]\nsay arr[1]",
        "let arr = [10, 20, 30]\nsay arr[1]"
    },
    {
        6,
        "Destructuring",
        "Unfish supports pattern destructuring for arrays and maps.\n"
        "Array: let [x, y] = [1, 2]\n"
        "Map: let {name, age} = person\n"
        "Rest pattern: let [first, ...rest] = items",
        "let [a, b] = [100, 200]\n"
        "say a + b",
        "Destructure [7, 8] into variables [x, y] and print their product x * y.",
        "56\n",
        "let [x, y] = [7, 8]\nsay x * y",
        "let [x, y] = [7, 8]\nsay x * y"
    },
    {
        7,
        "Structs & Methods",
        "Define custom types with 'struct Name: fields...'.\n"
        "Add methods with 'fn method(self, ...):'.",
        "struct Point:\n"
        "    x\n"
        "    y\n"
        "    fn sum(self):\n"
        "        return self.x + self.y\n"
        "\n"
        "let p = Point(3, 4)\n"
        "say p.sum()",
        "Create a struct 'Box' with field 'w', and a method 'area(self)' returning self.w * self.w. Create Box(5) and print its area.",
        "25\n",
        "struct Box:\n    w\n    fn area(self):\n        return self.w * self.w\nlet b = Box(5)\nsay b.area()",
        "struct Box:\n    w\n    fn area(self):\n        return self.w * self.w\nlet b = Box(5)\nsay b.area()"
    },
    {
        8,
        "Enums & Pattern Matching",
        "Unfish supports algebraic data types (enums and sum types):\n"
        "enum Result:\n"
        "    Ok(val)\n"
        "    Err(msg)\n"
        "Match on variants using 'match expr:' with 'when Variant(args):'.",
        "enum Color: Red, Green, Blue\n"
        "let c = Color.Green\n"
        "match c:\n"
        "    when Red:\n"
        "        say \"Red\"\n"
        "    when Green:\n"
        "        say \"Found green!\"",
        "Define an enum 'Option' with variant 'Some(v)' and 'None'. Create Option.Some(42) and print its payload using match.",
        "42\n",
        "enum Option:\n    Some(v)\n    None\nlet o = Option.Some(42)\nmatch o:\n    when Some(v):\n        say v",
        "enum Option:\n    Some(v)\n    None\nlet o = Option.Some(42)\nmatch o:\n    when Some(v):\n        say v"
    },
    {
        9,
        "Error Handling: Try, Catch, Finally",
        "Exceptions in Unfish are caught using try / catch / finally blocks.\n"
        "The finally block always runs, even if errors occur.",
        "try:\n"
        "    say 10 / 2\n"
        "catch err:\n"
        "    say \"caught error\"\n"
        "finally:\n"
        "    say \"cleanup done\"",
        "Write a try/catch block that catches a divide-by-zero (10 / 0) and prints \"recovered\".",
        "recovered\n",
        "try:\n    let x = 10 / 0\ncatch e:\n    say \"recovered\"",
        "try:\n    let x = 10 / 0\ncatch e:\n    say \"recovered\""
    },
    {
        10,
        "Fibers & Concurrency",
        "Unfish includes lightweight cooperative fibers and channels for messaging.\n"
        "Spawn a fiber with 'spawn(fn)', yield with 'yield()', and communicate via 'channel()'.",
        "let ch = channel(1)\n"
        "ch.send(\"message\")\n"
        "say ch.recv()",
        "Create a channel of capacity 1, send 99 into it, and print the received value.",
        "99\n",
        "let ch = channel(1)\nch.send(99)\nsay ch.recv()",
        "let ch = channel(1)\nch.send(99)\nsay ch.recv()"
    }
};

static const size_t NUM_LESSONS = sizeof(LESSONS) / sizeof(LESSONS[0]);

static void print_lesson(const Lesson* lesson) {
    printf("\n\033[1;36m======================================================================\033[0m\n");
    printf("\033[1;33mLesson %d/%zu: %s\033[0m\n", lesson->id, NUM_LESSONS, lesson->title);
    printf("\033[1;36m======================================================================\033[0m\n\n");

    printf("%s\n\n", lesson->description);
    printf("\033[1;32mExample:\033[0m\n");

    /* Indent example */
    char* ex_copy = strdup(lesson->example);
    char* line = strtok(ex_copy, "\n");
    while (line) {
        printf("  \033[37m%s\033[0m\n", line);
        line = strtok(NULL, "\n");
    }
    free(ex_copy);

    printf("\n\033[1;35mChallenge:\033[0m\n  %s\n\n", lesson->challenge);
    printf("Type your code below. Terminate with an empty line or press Ctrl+D.\n");
    printf("Special commands: \033[33m:hint\033[0m, \033[33m:solution\033[0m, \033[33m:next\033[0m, \033[33m:prev\033[0m, \033[33m:list\033[0m, \033[33m:quit\033[0m\n\n");
}

static bool evaluate_submission(const char* code, const char* expected_output) {
    UfArena arena;
    uf_arena_init(&arena, 65536);
    UfInterner interner;
    uf_interner_init(&interner, &arena);
    UfDiagnosticReporter reporter;
    uf_diag_reporter_init(&reporter, "<interactive>", code);

    UfLexer lexer;
    uf_lexer_init(&lexer, "<interactive>", code, &arena, &interner, &reporter);
    UfParser parser;
    uf_parser_init(&parser, &lexer, &arena, &reporter);

    UfProgram* prog = uf_parse_program(&parser);
    if (!prog || reporter.error_count > 0) {
        printf("\n\033[31mSyntax error in submission:\033[0m\n");
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        return false;
    }

    UfSemanticAnalyzer sema;
    uf_semantic_init(&sema, &arena, &reporter);
    if (!uf_analyze_program(&sema, prog) || reporter.error_count > 0) {
        printf("\n\033[31mSemantic error in submission:\033[0m\n");
        uf_interner_free(&interner);
        uf_arena_free(&arena);
        return false;
    }

    char* mem_buf = NULL;
    size_t mem_size = 0;
    FILE* mem_stream = open_memstream(&mem_buf, &mem_size);

    UfRuntime rt;
    uf_runtime_init(&rt, &reporter);
    rt.out_stream = mem_stream;
    rt.err_stream = mem_stream;

    uf_interpret_program(&rt, prog);
    fflush(mem_stream);
    fclose(mem_stream);

    bool matched = false;
    if (!rt.had_runtime_error && mem_buf) {
        if (strcmp(mem_buf, expected_output) == 0) {
            matched = true;
        } else {
            /* Check if expected_output substring is present */
            if (strstr(mem_buf, expected_output) != NULL) {
                matched = true;
            }
        }
    }

    if (matched) {
        printf("\n\033[1;32mOutput:\033[0m\n%s", mem_buf ? mem_buf : "");
        printf("\033[1;32m🎉 Correct! Challenge completed successfully.\033[0m\n");
    } else {
        printf("\n\033[1;31mOutput did not match challenge requirement.\033[0m\n");
        if (mem_buf && mem_buf[0] != '\0') {
            printf("Your output:\n%s\n", mem_buf);
        } else {
            printf("Your output was empty or produced an error.\n");
        }
        printf("Expected output:\n%s\n", expected_output);
    }

    if (mem_buf) free(mem_buf);
    uf_runtime_free(&rt);
    uf_interner_free(&interner);
    uf_arena_free(&arena);
    return matched;
}

int uf_learn_start(int starting_lesson) {
    int current = starting_lesson - 1;
    if (current < 0 || (size_t)current >= NUM_LESSONS) {
        current = 0;
    }

    printf("\n🐡 Welcome to Unfish Interactive Tutorial!\n");
    printf("Learn Unfish in %zu interactive hands-on lessons.\n", NUM_LESSONS);

    while (current >= 0 && (size_t)current < NUM_LESSONS) {
        const Lesson* lesson = &LESSONS[current];
        print_lesson(lesson);

        UfStrBuf input_buf;
        uf_strbuf_init(&input_buf);

        char line_buf[1024];
        bool read_any = false;

        for (;;) {
            printf("unfish-learn (%d)> ", lesson->id);
            fflush(stdout);
            if (!fgets(line_buf, sizeof(line_buf), stdin)) {
                printf("\nExiting tutorial. Keep coding!\n");
                uf_strbuf_free(&input_buf);
                return 0;
            }

            /* Strip trailing newline */
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\r' || line_buf[len - 1] == '\n')) {
                line_buf[--len] = '\0';
            }

            /* Check commands */
            if (strcmp(line_buf, ":next") == 0) {
                if ((size_t)current + 1 < NUM_LESSONS) current++;
                else printf("You're on the final lesson!\n");
                break;
            } else if (strcmp(line_buf, ":prev") == 0) {
                if (current > 0) current--;
                else printf("You're on the first lesson!\n");
                break;
            } else if (strcmp(line_buf, ":hint") == 0) {
                printf("\n\033[1;33mHint:\033[0m %s\n\n", lesson->hint);
                continue;
            } else if (strcmp(line_buf, ":solution") == 0) {
                printf("\n\033[1;36mSolution:\033[0m\n%s\n\n", lesson->solution);
                continue;
            } else if (strcmp(line_buf, ":list") == 0) {
                printf("\n\033[1;34mCurriculum:\033[0m\n");
                for (size_t l = 0; l < NUM_LESSONS; ++l) {
                    printf("  %s %zu. %s\n", (int)l == current ? "->" : "  ", l + 1, LESSONS[l].title);
                }
                printf("\n");
                continue;
            } else if (strcmp(line_buf, ":quit") == 0 || strcmp(line_buf, ":exit") == 0) {
                printf("\nExiting tutorial. Keep coding!\n");
                uf_strbuf_free(&input_buf);
                return 0;
            }

            if (len == 0) {
                /* Empty line submitted -> evaluate */
                if (read_any) {
                    char* code = uf_strbuf_detach(&input_buf);
                    bool ok = evaluate_submission(code, lesson->expected_output);
                    free(code);
                    uf_strbuf_init(&input_buf);
                    read_any = false;
                    if (ok) {
                        printf("Type \033[33m:next\033[0m to proceed to next lesson.\n\n");
                    }
                }
                continue;
            }

            uf_strbuf_append(&input_buf, line_buf);
            uf_strbuf_append_char(&input_buf, '\n');
            read_any = true;
        }

        uf_strbuf_free(&input_buf);
    }

    printf("\n🎓 Congratulations! You completed all lessons in the Unfish Tutorial!\n\n");
    return 0;
}

void uf_learn_list(void) {
    printf("\n🐡 Unfish Interactive Tutorial Curriculum:\n");
    for (size_t l = 0; l < NUM_LESSONS; ++l) {
        printf("  %2zu. %s\n", l + 1, LESSONS[l].title);
    }
    printf("\nStart any lesson with: unfish learn <number>\n\n");
}

