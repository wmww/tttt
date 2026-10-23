// tttt's overlay (test/oracle, plan 54): mapped into tsgo's checkout as internal/testrunner/tttt_probe_test.go.
// TestTtttProbe runs tsgo's compiler baseline runner on every test file under $TTTT_TESTDATA/tests/cases/
// {compiler,conformance} (testdata.go points the harness there) and leaves the baselines (.errors.txt, .types,
// .symbols, .js...) in $TTTT_TESTDATA/baselines/local/SUITE: exactly what the native suite's reference baselines
// would be for those files. scripts/tsgoprobe.sh runs it. With TTTT_PROBE_RERUN set it runs again after each line of
// stdin (test/diffapp's protocol), printing "[tttt runner] Done running tests" after each run.
//
// A watchdog fails loudly instead of hanging: a test configuration running longer than $TTTT_PROBE_TIMEOUT seconds
// (default 120) prints "[tttt runner] TIMEOUT <file>" for each such test and exits with status 3 (a goroutine can't be
// killed; tsgoprobe.sh sets the file aside and reruns the others). Every $TTTT_PROBE_PROGRESS (default 500) finished
// configurations "[tttt runner] progress N done" is printed.
package testrunner

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"sync"
	"testing"
	"time"

	"github.com/microsoft/TypeScript/tsc/internal/testutil"
	"github.com/microsoft/TypeScript/tsc/internal/testutil/harnessutil"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
)

func ttttEnvInt(name string, def int) int {
	if n, err := strconv.Atoi(os.Getenv(name)); err == nil && n > 0 {
		return n
	}
	return def
}

var (
	ttttMu       sync.Mutex
	ttttRunning  = map[*testing.T]ttttRun{}
	ttttDone     int
	ttttTimeout  = time.Duration(ttttEnvInt("TTTT_PROBE_TIMEOUT", 120)) * time.Second
	ttttProgress = ttttEnvInt("TTTT_PROBE_PROGRESS", 500)
	ttttWatchdog sync.Once
)

type ttttRun struct {
	file  string
	start time.Time
}

// ttttWatch: t (a test configuration, past its t.Parallel()) is running; done when its cleanup runs
func ttttWatch(t *testing.T, file string) {
	ttttWatchdog.Do(func() {
		go func() {
			for range time.Tick(time.Second) {
				ttttMu.Lock()
				var late []string
				for _, r := range ttttRunning {
					if time.Since(r.start) > ttttTimeout {
						late = append(late, r.file)
					}
				}
				if len(late) > 0 {
					sort.Strings(late)
					for _, f := range late {
						fmt.Printf("[tttt runner] TIMEOUT %s (over %v)\n", f, ttttTimeout)
					}
					os.Exit(3)
				}
				ttttMu.Unlock()
			}
		}()
	})
	ttttMu.Lock()
	ttttRunning[t] = ttttRun{file, time.Now()}
	ttttMu.Unlock()
	t.Cleanup(func() {
		ttttMu.Lock()
		delete(ttttRunning, t)
		ttttDone++
		if ttttDone%ttttProgress == 0 {
			fmt.Printf("[tttt runner] progress %d done\n", ttttDone)
		}
		ttttMu.Unlock()
	})
}

// runTest and runSingleConfigTest of compiler_runner.go with the watchdog: the copied part between "copy:" markers
// must stay identical to the original's (scripts/overlaycheck.sh compares them)
func (r *CompilerBaselineRunner) ttttRunTest(t *testing.T, filename string) {
	test := getCompilerFileBasedTest(t, filename)
	basename := tspath.GetBaseFileName(filename)
	if len(test.configurations) > 0 {
		for _, config := range test.configurations {
			testName := basename
			if config.Name != "" {
				testName += " " + config.Name
			}
			t.Run(testName, func(t *testing.T) { r.ttttRunSingleConfigTest(t, testName, test, config) })
		}
	} else {
		t.Run(basename, func(t *testing.T) { r.ttttRunSingleConfigTest(t, basename, test, nil) })
	}
}

func (r *CompilerBaselineRunner) ttttRunSingleConfigTest(t *testing.T, testName string, test *compilerFileBasedTest, config *harnessutil.NamedTestConfiguration) {
	t.Parallel()
	ttttWatch(t, test.filename)
	// copy:begin
	defer testutil.RecoverAndFail(t, "Panic on compiler test "+test.filename)

	payload := makeUnitsFromTest(test.content, test.filename)
	compilerTest := newCompilerTest(t, testName, test.filename, &payload, config)

	harnessutil.SkipUnsupportedCompilerOptions(t, compilerTest.options)

	compilerTest.verifyDiagnostics(t, r.testSuitName)
	compilerTest.verifyContentMapper(t, r.testSuitName)
	compilerTest.verifyJavaScriptOutput(t, r.testSuitName)
	compilerTest.verifySourceMapOutput(t, r.testSuitName)
	compilerTest.verifySourceMapRecord(t, r.testSuitName)
	compilerTest.verifyTypesAndSymbols(t, r.testSuitName)
	compilerTest.verifyModuleResolution(t, r.testSuitName)
	compilerTest.verifyUnionOrdering(t)
	compilerTest.verifyParentPointers(t)
	// copy:end
}

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
					r.ttttRunTest(t, filename)
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
