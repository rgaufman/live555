// The ONE check helper for the suites that use the `check(bool, what)` dialect.
//
// It was copy-pasted per suite -- seven private copies across two INCOMPATIBLE argument orders
// (`check(cond, what)` here vs `ok(what, cond)` in the lifecycle/recovery suites): moving an
// assertion between suites silently transposed condition and message, and both compiled, because
// bool and char const* convert. One definition ends the copy drift; the reversed-order suites and
// the CHECK-macro suite keep their local style deliberately (converting them is churn without a
// defect), but no NEW suite should define its own.
//
// Header-only and self-contained, like everything else in this directory. The Makefile's header
// wildcard includes test/*.hh, so editing this rebuilds every suite that uses it.

#ifndef _TEST_CHECK_HH
#define _TEST_CHECK_HH

#include <stdio.h>

static int g_checkFailures = 0;

static inline void check(bool ok, char const* what) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++g_checkFailures; }
}

static inline void checkEq(unsigned got, unsigned want, char const* what) {
  if (got != want) {
    fprintf(stderr, "FAIL: %s (got %u, want %u)\n", what, got, want);
    ++g_checkFailures;
  }
}

// Print the verdict and return the process exit code: 0 green, 1 red.
static inline int checkSummary(char const* suiteName) {
  if (g_checkFailures == 0) {
    printf("%s: all checks passed\n", suiteName);
    return 0;
  }
  fprintf(stderr, "%s: %d check(s) failed\n", suiteName, g_checkFailures);
  return 1;
}

#endif
