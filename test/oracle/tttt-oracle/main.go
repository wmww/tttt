// tttt's overlay (test/oracle, plan 54): mapped into tsgo's checkout as cmd/tttt-oracle/main.go. tsgo's answers for
// many projects in one process, the sweeps' oracle (scripts/sweep.py, sourcesweep.py, expecttypes.py, flagflip.py).
// Reads project directories (each with a tsconfig.json) from stdin, one per line, and prints one JSON line per
// project when it's done with it:
//
//	tttt-oracle [--flag...]           {"project", "ms", "status", "diags": [[file, line, col, code], ...]}
//	tttt-oracle --expect [--flag...]  {"project", "ms", "asserts": [[file, line, col, endLine, endCol, kind, actual, expected], ...]}
//	tttt-oracle --type-at PROJECT FILE LINE|* NAME   (no stdin: `line name : type` per identifier, scripts/tsctypeat.sh)
//
// The diagnostics are exactly what `tsc -p DIR --noEmit --pretty false --singleThreaded [--flag...]` prints (tsgo's
// own command line runs in-process; its output is parsed back): tttt is a drop-in `tsc --noEmit`. Paths are relative
// to the working directory like the CLI's, line and col 1-based (col in UTF-16 units), a diagnostic without a file is
// ["", 0, 0, code]. status is the CLI's exit code. Other `--flag` arguments are passed on (boolean options: `--strict`).
//
// --expect prints the `$ExpectType` assertions of the project's own files instead: the node DefinitelyTyped's `expect`
// lint rule checks for each (its start and end, 1-based, columns in UTF-16 units), its kind's name, and what the rule
// compares: typeToString(getTypeAtLocation(node), undefined, NoTruncation). Types are read after checking every file
// but the default libs with one checker (union member order is type creation order, which is check order).
package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/bundled"
	"github.com/microsoft/TypeScript/tsc/internal/checker"
	"github.com/microsoft/TypeScript/tsc/internal/collections"
	"github.com/microsoft/TypeScript/tsc/internal/compiler"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/execute"
	"github.com/microsoft/TypeScript/tsc/internal/execute/tsc"
	"github.com/microsoft/TypeScript/tsc/internal/scanner"
	"github.com/microsoft/TypeScript/tsc/internal/tsoptions"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
	"github.com/microsoft/TypeScript/tsc/internal/vfs"
	"github.com/microsoft/TypeScript/tsc/internal/vfs/osvfs"
	"github.com/microsoft/TypeScript/tsc/internal/vfs/wrapvfs"
)

type sys struct {
	out   bytes.Buffer
	fs    vfs.FS
	cwd   tspath.RootedDirectoryPath
	start time.Time
}

func (s *sys) Writer() io.Writer                                  { return &s.out }
func (s *sys) ErrorWriter() io.Writer                             { return os.Stderr }
func (s *sys) FS() vfs.FS                                         { return s.fs }
func (s *sys) DefaultLibraryPath() tspath.RootedDirectoryPath     { return bundled.LibPath() }
func (s *sys) GetCurrentDirectory() tspath.RootedDirectoryPath    { return s.cwd }
func (s *sys) WriteOutputIsTTY() bool                             { return false }
func (s *sys) GetWidthOfTerminal() int                            { return 0 }
func (s *sys) GetEnvironmentVariable(name string) (string, bool)  { return "", false }
func (s *sys) Now() time.Time                                     { return time.Now() }
func (s *sys) SinceStart() time.Duration                          { return time.Since(s.start) }
func (s *sys) Spawn(command []string, dir string, stderr io.Writer) (io.ReadWriteCloser, error) {
	return nil, fmt.Errorf("tttt-oracle doesn't spawn processes")
}

var _ tsc.System = (*sys)(nil)

func newSys() *sys {
	cwd, err := os.Getwd()
	if err != nil {
		panic(err)
	}
	return &sys{fs: withoutBuildInfo(bundled.WrapFS(osvfs.FS())), cwd: tspath.RootedDirectoryPathFromAbsolute(cwd), start: time.Now()}
}

// Build info is neither read nor written: tsc --noEmit reads an incremental project's .tsbuildinfo and skips checking
// what it says is unchanged (its time then means nothing), and writes one into the checkout (webcmd's dist/). Every
// run is a cold check, like perfsweep's. (A config's tsBuildInfoFile may be named anything with it: better-auth's
// `.cache/ts/tsbuildinfo`, karakeep's `node_modules/.cache/tsbuildinfo.json`)
func withoutBuildInfo(fs vfs.FS) vfs.FS {
	buildInfo := func(p string) bool { return strings.Contains(tspath.GetBaseFileName(p), "tsbuildinfo") }
	return wrapvfs.Wrap(fs, wrapvfs.Replacements{
		FileExists: func(p tspath.RootedFilePath) bool { return !buildInfo(string(p)) && fs.FileExists(p) },
		ReadFile: func(p tspath.RootedFilePath) (string, bool) {
			if buildInfo(string(p)) {
				return "", false
			}
			return fs.ReadFile(p)
		},
		WriteFile: func(p tspath.RootedFilePath, data string) error {
			if buildInfo(string(p)) {
				return nil
			}
			return fs.WriteFile(p, data)
		},
		Stat: func(p tspath.RootedPath) vfs.FileInfo {
			if buildInfo(string(p)) {
				return nil
			}
			return fs.Stat(p)
		},
	})
}

// The CLI's `--pretty false` format (diagnosticwriter.WriteFormatDiagnostic): `file(line,col): error TS1: message`,
// or `error TS1: message` without a file; message chains and related information are indented below
var (
	withFile    = regexp.MustCompile(`^(.+)\((\d+),(\d+)\): error TS(\d+): `)
	withoutFile = regexp.MustCompile(`^error TS(\d+): `)
)

func diagnose(dir string, flags []string) map[string]any {
	start := time.Now()
	s := newSys()
	args := append([]string{"-p", dir, "--noEmit", "--pretty", "false", "--singleThreaded"}, flags...)
	result := execute.CommandLine(context.Background(), s, args, nil)
	diags := [][]any{}
	for line := range strings.SplitSeq(s.out.String(), "\n") {
		line = strings.TrimSuffix(line, "\r")
		if m := withFile.FindStringSubmatch(line); m != nil {
			l, _ := strconv.Atoi(m[2])
			c, _ := strconv.Atoi(m[3])
			code, _ := strconv.Atoi(m[4])
			diags = append(diags, []any{m[1], l, c, code})
		} else if m := withoutFile.FindStringSubmatch(line); m != nil {
			code, _ := strconv.Atoi(m[1])
			diags = append(diags, []any{"", 0, 0, code})
		}
	}
	return map[string]any{"project": dir, "ms": time.Since(start).Milliseconds(), "status": int(result.Status), "diags": diags}
}

// The program `tsc -p DIR` would check (tscCompilation's steps without the reporting), or nil if the command line or
// the config has unrecoverable errors
func program(s *sys, dir string, flags []string) *compiler.Program {
	commandLine := tsoptions.ParseCommandLine(append([]string{"-p", dir, "--noEmit", "--singleThreaded"}, flags...), s.fs, s.cwd)
	if len(commandLine.Errors) > 0 {
		if os.Getenv("TTTT_ORACLE_DEBUG") != "" {
			fmt.Fprintln(os.Stderr, "command line:", commandLine.Errors[0].String())
		}
		return nil
	}
	project := commandLine.CompilerOptions().Project
	var configFileName tspath.RootedFilePath
	if s.fs.FileExists(tspath.RootedFilePathFromPath(project)) { // (-p a tsconfig file rather than its directory)
		configFileName = tspath.RootedFilePathFromPath(project)
	} else {
		configFileName = tspath.RootedDirectoryPathFromPath(project).ResolveFile("tsconfig.json")
	}
	raw := commandLine.Raw.(*collections.OrderedMap[string, any])
	wrapped := &collections.OrderedMap[string, any]{}
	wrapped.Set("compilerOptions", raw)
	cache := &tsc.ExtendedConfigCache{}
	config, errors := tsoptions.GetParsedCommandLineOfConfigFilePath(configFileName,
		s.fs.CaseSensitivity().PathKey(tspath.RootedPath(configFileName)), commandLine.CompilerOptions(), wrapped, s.fs, cache)
	if len(errors) > 0 {
		if os.Getenv("TTTT_ORACLE_DEBUG") != "" {
			fmt.Fprintln(os.Stderr, "config:", errors[0].String())
		}
		return nil
	}
	host := compiler.NewCachedFSCompilerHost(s.fs, s.DefaultLibraryPath(), cache, nil, nil)
	return compiler.NewProgram(compiler.ProgramOptions{
		ProgramConfig: compiler.ProgramConfig{Config: config, SingleThreaded: core.TSTrue},
		ProgramHosts:  compiler.ProgramHosts{Host: host},
	})
}

var assertionComment = regexp.MustCompile(`//(.*)`)
var assertionText = regexp.MustCompile(`^ ?\$Expect(TypeSnapshot|Type|Error)( (.*))?$`)
var blank = regexp.MustCompile(`^\s*$`)

// The `$ExpectType` assertions of the project's own files (DefinitelyTyped's eslint-plugin, rules/expect.ts:
// parseAssertions and getExpectTypeFailures). A comment that is the first thing on its line asserts the next line;
// the node is the first one in forEachChild's pre-order that starts on the asserted line, an expression statement
// unwrapped, a variable statement of one initialized declaration its initializer.
func expectTypes(dir string, flags []string) map[string]any {
	start := time.Now()
	s := newSys()
	out := [][]any{}
	p := program(s, dir, flags)
	if p == nil {
		return map[string]any{"project": dir, "ms": time.Since(start).Milliseconds(), "asserts": out}
	}
	ctx := context.Background()
	c, done := p.GetTypeChecker(ctx)
	defer done()
	for _, f := range p.GetSourceFiles() {
		if !p.IsSourceFileDefaultLibrary(f.PathKey()) {
			c.GetDiagnostics(ctx, f)
		}
	}
	root := tspath.RootedDirectoryPathFromPath(tspath.ToRootedPath(dir, s.cwd)).AsString() + "/"
	for _, file := range p.GetSourceFiles() {
		name := file.FileName().AsString()
		if !strings.HasPrefix(name, root) || strings.Contains(name, "/node_modules/") {
			continue
		}
		text := file.Text()
		if !strings.Contains(text, "$ExpectType") {
			continue
		}
		lineStarts := scanner.GetECMALineStarts(file)
		assertions := map[int]string{}
		curLine := 0
		for _, m := range assertionComment.FindAllStringSubmatchIndex(text, -1) {
			match := assertionText.FindStringSubmatch(text[m[2]:m[3]])
			if match == nil || match[1] != "Type" || match[2] == "" {
				continue
			}
			for curLine+1 < len(lineStarts) && int(lineStarts[curLine+1]) <= m[0] {
				curLine++
			}
			line := curLine
			if blank.MatchString(text[lineStarts[curLine]:m[0]]) {
				line++
			}
			if _, ok := assertions[line]; !ok {
				assertions[line] = strings.TrimSpace(match[3])
			}
		}
		relative := tspath.GetRelativePathFromDirectory(s.cwd.AsString(), name, s.fs.CaseSensitivity())
		var iterate func(node *ast.Node) bool
		iterate = func(node *ast.Node) bool {
			if node.Kind == ast.KindEndOfFile {
				return false
			}
			nodeStart := scanner.GetTokenPosOfNode(node, file, false)
			line, _ := scanner.GetECMALineAndUTF16CharacterOfPosition(file, nodeStart)
			if expected, ok := assertions[line]; ok {
				if ast.IsExpressionStatement(node) {
					node = node.Expression()
				}
				if ast.IsVariableStatement(node) {
					declarations := node.AsVariableStatement().DeclarationList.AsVariableDeclarationList().Declarations.Nodes
					if len(declarations) == 1 && declarations[0].Initializer() != nil {
						node = declarations[0].Initializer()
					}
				}
				actual := ""
				if t := c.GetTypeAtLocation(node); t != nil {
					actual = c.TypeToStringEx(t, nil, checker.TypeFormatFlagsNoTruncation, nil)
				}
				sl, sc := scanner.GetECMALineAndUTF16CharacterOfPosition(file, scanner.GetTokenPosOfNode(node, file, false))
				el, ec := scanner.GetECMALineAndUTF16CharacterOfPosition(file, node.End())
				out = append(out, []any{relative, sl + 1, int(sc) + 1, el + 1, int(ec) + 1, node.Kind.String()[len("Kind"):], actual, expected})
				delete(assertions, line)
			}
			node.ForEachChild(iterate)
			return false
		}
		file.AsNode().ForEachChild(iterate)
	}
	return map[string]any{"project": dir, "ms": time.Since(start).Milliseconds(), "asserts": out}
}

// What an identifier's type is inside a project (scripts/tsctypeat.sh): each identifier NAME on line LINE ("*": every
// line) of FILE (relative to the project directory, or to a tsconfig's), as `line name : type`, after checking the file
func typeAt(dir, file, line, name string) {
	s := newSys()
	p := program(s, dir, nil)
	if p == nil {
		fmt.Fprintln(os.Stderr, "the project has option errors")
		os.Exit(1)
	}
	base := tspath.ToRootedDirectoryPath(dir, s.cwd)
	if strings.HasSuffix(dir, ".json") { // (a tsconfig file: FILE is relative to its directory)
		base = tspath.ToRootedFilePath(dir, s.cwd).Directory()
	}
	source := p.GetSourceFile(tspath.ToRootedFilePath(file, base))
	if source == nil {
		fmt.Fprintln(os.Stderr, file, "isn't part of the program")
		os.Exit(1)
	}
	ctx := context.Background()
	c, done := p.GetTypeChecker(ctx)
	defer done()
	c.GetDiagnostics(ctx, source)
	var visit func(node *ast.Node) bool
	visit = func(node *ast.Node) bool {
		if ast.IsIdentifier(node) && node.Text() == name {
			at, _ := scanner.GetECMALineAndUTF16CharacterOfPosition(source, scanner.GetTokenPosOfNode(node, source, false))
			if line == "*" || strconv.Itoa(at+1) == line {
				fmt.Println(at+1, name, ":", c.TypeToString(c.GetTypeAtLocation(node)))
			}
		}
		node.ForEachChild(visit)
		return false
	}
	source.AsNode().ForEachChild(visit)
}

func main() {
	if len(os.Args) == 6 && os.Args[1] == "--type-at" {
		typeAt(os.Args[2], os.Args[3], os.Args[4], os.Args[5])
		return
	}
	expect := false
	var flags []string
	for _, a := range os.Args[1:] {
		if a == "--expect" {
			expect = true
		} else {
			flags = append(flags, a)
		}
	}
	in := bufio.NewScanner(os.Stdin)
	out := json.NewEncoder(os.Stdout)
	out.SetEscapeHTML(false)
	for in.Scan() {
		dir := strings.TrimSpace(in.Text())
		if dir == "" {
			continue
		}
		var result map[string]any
		func() {
			defer func() {
				if r := recover(); r != nil {
					result = map[string]any{"project": dir, "ms": 0, "crash": fmt.Sprint(r)[:min(500, len(fmt.Sprint(r)))], "diags": [][]any{}}
				}
			}()
			if expect {
				result = expectTypes(dir, flags)
			} else {
				result = diagnose(dir, flags)
			}
		}()
		if err := out.Encode(result); err != nil {
			panic(err)
		}
	}
}
