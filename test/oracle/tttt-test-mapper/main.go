// tttt's overlay (test/oracle, plan 56): mapped into tsgo's checkout as cmd/tttt-test-mapper/main.go. tsgo's suite
// serves its content mappers in process (contentmappertest's spawner, by a manifest's exec[0]); this is the same
// handler as a process: `tttt-test-mapper compiler-test-mapper` speaks the content mapper protocol on stdin/stdout,
// so tttt's harness can run a test's manifest `exec` as [this binary, exec...] against its real host.
package main

import (
	"fmt"
	"os"

	"github.com/microsoft/TypeScript/tsc/internal/testutil/contentmappertest"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: tttt-test-mapper MAPPER-NAME [ARGS...]")
		os.Exit(2)
	}
	if err := contentmappertest.TtttServeStdio(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "tttt-test-mapper:", err)
		os.Exit(1)
	}
}
