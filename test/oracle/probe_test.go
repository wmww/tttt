// tttt's overlay (test/oracle, plan 54): mapped into tsgo's checkout as internal/testrunner/tttt_probe_test.go.
// TestTtttProbe runs tsgo's compiler baseline runner on every test file under $TTTT_TESTDATA/tests/cases/
// {compiler,conformance} (testdata.go points the harness there) and leaves the baselines (.errors.txt, .types,
// .symbols, .js...) in $TTTT_TESTDATA/baselines/local/SUITE: exactly what the native suite's reference baselines
// would be for those files. scripts/tsgoprobe.sh runs it. With TTTT_PROBE_RERUN set it runs again after each line of
// stdin (test/diffapp's protocol), printing "[tttt runner] Done running tests" after each run.
package testrunner

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

func TestTtttProbe(t *testing.T) { //nolint:paralleltest
	if os.Getenv("TTTT_TESTDATA") == "" {
		t.Skip("TTTT_TESTDATA is not set")
	}
	run := func() {
		for _, testType := range []CompilerTestType{TestTypeRegression, TestTypeConformance} {
			r := NewCompilerBaselineRunner(testType)
			r.cleanUpLocal(t)
			if _, err := os.Stat(filepath.Join(os.Getenv("TTTT_TESTDATA"), r.basePath)); err != nil {
				continue
			}
			// (Each run's subtests finish before the next run: t.Run returns when its parallel subtests have)
			t.Run(r.testSuitName, func(t *testing.T) {
				for _, filename := range r.EnumerateTestFiles() {
					r.runTest(t, filename)
				}
			})
		}
		fmt.Println("[tttt runner] Done running tests")
	}
	run()
	if os.Getenv("TTTT_PROBE_RERUN") != "" {
		for in := bufio.NewScanner(os.Stdin); in.Scan(); {
			run()
		}
	}
}
