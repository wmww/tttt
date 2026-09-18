#pragma once

#include "prelude.h"

// Shows the difference between the two given strings by piping them into the `diff` command. Diff's output goes to our
// stdout/stderr. Errors are logged, they are not worth interrupting a test run for.
void run_diff_subprocess(csview a, csview b);
