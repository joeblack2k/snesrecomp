#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tier2_capture.h"

#define CHECK(cond, ...) do { \
    if (!(cond)) { fprintf(stdout, "FAIL: "); fprintf(stdout, __VA_ARGS__); \
                   fputc('\n', stdout); return 1; } \
} while (0)

int main(void) {
    const char *log = "tier2_capture_failure.stderr";
    remove(log);
#ifdef _WIN32
    _putenv_s("SNESRECOMP_TIER2_JOURNAL",
              "missing-tier2-directory/journal.jsonl");
#else
    setenv("SNESRECOMP_TIER2_JOURNAL",
           "missing-tier2-directory/journal.jsonl", 1);
#endif
    CHECK(freopen(log, "w", stderr) != NULL, "cannot redirect stderr");
    CHECK(!tier2_capture_append_discovery(
              "QA Game", 0x808000, 0xC01234, "M1X1", "call_gap", 1, 12),
          "unwritable first append unexpectedly succeeded");
    CHECK(!tier2_capture_append_discovery(
              "QA Game", 0x808100, 0xC05678, "M0X1", "bank_miss", 0, 34),
          "disabled second append unexpectedly succeeded");
    CHECK(fflush(stderr) == 0, "cannot flush stderr");

    FILE *in = fopen(log, "r");
    CHECK(in != NULL, "cannot reopen stderr");
    char line[512];
    int failures = 0;
    while (fgets(line, sizeof line, in)) {
        if (strstr(line, "cannot append dispatch-miss journal")) failures++;
    }
    fclose(in);
    remove(log);
    CHECK(failures == 1, "expected one failure report, got %d", failures);
    puts("tier2_capture_failure_test: PASS");
    return 0;
}
