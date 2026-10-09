// tttt's overlay (test/oracle, plan 54): mapped into tsgo's checkout as internal/repo/tttt_testdata.go. With
// TTTT_TESTDATA set, the test data root is that directory instead of the checkout's testdata: the harness reads
// tests/cases/SUITE and tests/lib there, writes baselines/local/SUITE there, and prints `//// [tests/cases/...]`
// headers relative to it, so a probe directory looks like the suite's own tree.
package repo

import (
	"os"
	"path/filepath"
)

func init() {
	if dir := os.Getenv("TTTT_TESTDATA"); dir != "" {
		abs, err := filepath.Abs(dir)
		if err != nil {
			panic(err)
		}
		testDataPath = func() string { return abs }
	}
}
