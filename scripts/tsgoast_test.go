// scripts/tsgoast.sh (kept out of test/oracle, whose hash rebuilds the shared oracle tools): tsgo's parse tree and parse diagnostics for one file (TTTT_SRC), mapped by `go test -overlay`
// to internal/parser/tttt_ast_test.go. The script kind follows the file's extension (.ts by default).
package parser_test

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/parser"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
)

func TestTtttAst(t *testing.T) {
	src, err := os.ReadFile(os.Getenv("TTTT_SRC"))
	if err != nil {
		t.Fatal(err)
	}
	text := string(src)
	ext := strings.ToLower(filepath.Ext(os.Getenv("TTTT_SRC")))
	kind := map[string]core.ScriptKind{".js": core.ScriptKindJS, ".cjs": core.ScriptKindJS, ".mjs": core.ScriptKindJS,
		".jsx": core.ScriptKindJSX, ".tsx": core.ScriptKindTSX, ".json": core.ScriptKindJSON}[ext]
	if kind == core.ScriptKindUnknown {
		kind, ext = core.ScriptKindTS, ".ts"
	}
	name := "/a" + ext
	file := parser.ParseSourceFile(ast.SourceFileParseOptions{FileName: tspath.RootedFilePath(name), PathKey: tspath.PathKey(name)}, text, kind)
	var visit func(n *ast.Node, depth int) bool
	visit = func(n *ast.Node, depth int) bool {
		s := ""
		if n.Pos() >= 0 && n.End() <= len(text) && n.Pos() <= n.End() {
			s = text[n.Pos():n.End()]
		}
		fmt.Printf("%s%v [%d,%d) %q\n", strings.Repeat("  ", depth), n.Kind, n.Pos(), n.End(), s)
		n.ForEachChild(func(c *ast.Node) bool { return visit(c, depth+1) })
		return false
	}
	for _, st := range file.Statements.Nodes {
		visit(st, 0)
	}
	for _, d := range file.Diagnostics() {
		fmt.Printf("diag TS%d at %d len %d: %s\n", d.Code(), d.Pos(), d.Len(), d.String())
	}
}
