// tsc 5.8's diagnostics for many projects in one process (./sweep.py's oracle, plan 44): reads project directories
// (each with a tsconfig.json) from stdin, one per line, and prints one JSON line per project:
// {"project", "ms", "diags": [[file, line, col, code], ...]} with paths relative to the working directory, line and
// col 1-based. The lib files' source files are shared between projects (parsing lib.dom.d.ts for every project
// would be most of the time); everything else is parsed per project, like `tsc -p DIR` would.
// With `--expect` (./expecttypes.py's oracle, plan 44 stage 3 item 2) it prints the `$ExpectType` assertions instead:
// {"project", "ms", "asserts": [[file, line, col, endLine, endCol, kind, actual, expected], ...]}, each the node
// DefinitelyTyped's `expect` lint rule checks (its start and end, 1-based, columns in UTF-16 units like tsc's), its
// SyntaxKind's name, and what that rule compares: typeToString(getTypeAtLocation(node), undefined, NoTruncation).
const ts = require(require('path').resolve(__dirname, '../thirdparty/tsc/lib/typescript.js'));
const path = require('path');
const readline = require('readline');

const expectMode = process.argv.includes('--expect');
const libDir = path.dirname(ts.getDefaultLibFilePath({}));
const libCache = new Map();

function check(dir) {
    const start = Date.now();
    const configPath = path.resolve(dir, 'tsconfig.json');
    const diags = [];
    const parsed = ts.getParsedCommandLineOfConfigFile(configPath, {}, {
        ...ts.sys,
        onUnRecoverableConfigFileDiagnostic: d => diags.push(d),
    });
    if (parsed) {
        const host = ts.createCompilerHost(parsed.options);
        const getSourceFile = host.getSourceFile;
        host.getSourceFile = (fileName, languageVersionOrOptions, onError, shouldCreate) => {
            if (path.dirname(fileName) !== libDir)
                return getSourceFile(fileName, languageVersionOrOptions, onError, shouldCreate);
            const options = typeof languageVersionOrOptions === 'object' ? languageVersionOrOptions : { languageVersion: languageVersionOrOptions };
            const key = `${fileName}|${options.languageVersion}|${options.impliedNodeFormat}|${options.jsDocParsingMode}`;
            let file = libCache.get(key);
            if (!file) {
                file = getSourceFile(fileName, languageVersionOrOptions, onError, shouldCreate);
                libCache.set(key, file);
            }
            return file;
        };
        const program = ts.createProgram({
            rootNames: parsed.fileNames, options: parsed.options, host,
            configFileParsingDiagnostics: ts.getConfigFileParsingDiagnostics(parsed),
            projectReferences: parsed.projectReferences,
        });
        // Types are read after checking every file but the default libs, like tttt does (and the suite harness, with
        // skipDefaultLibCheck): union member order is type creation order, and asking getTypeAtLocation lazily
        // creates the declarations' types in another order than checking them does (chrome's enum strings)
        if (expectMode) {
            for (const f of program.getSourceFiles()) if (!program.isSourceFileDefaultLibrary(f)) program.getSemanticDiagnostics(f);
            return { project: dir, ms: Date.now() - start, asserts: expectTypes(dir, program) };
        }
        // What `tsc -p` reports (emitFilesAndReportErrors): config, options, global, syntactic, then semantic
        // diagnostics if there were no syntactic ones; declaration diagnostics with `declaration` on
        diags.push(...program.getConfigFileParsingDiagnostics(), ...program.getOptionsDiagnostics(),
            ...program.getGlobalDiagnostics());
        const syntactic = program.getSyntacticDiagnostics();
        diags.push(...syntactic);
        if (syntactic.length === 0) {
            diags.push(...program.getSemanticDiagnostics());
            if (ts.getEmitDeclarations(parsed.options)) diags.push(...program.getDeclarationDiagnostics());
        }
    }
    const out = diags.map(d => {
        if (!d.file) return ['', 0, 0, d.code];
        const { line, character } = d.file.getLineAndCharacterOfPosition(d.start);
        return [path.relative(process.cwd(), d.file.fileName), line + 1, character + 1, d.code];
    });
    return { project: dir, ms: Date.now() - start, diags: out };
}

// The `$ExpectType` assertions of the project's own files (DefinitelyTyped's eslint-plugin, rules/expect.ts:
// parseAssertions and getExpectTypeFailures). A comment that is the first thing on its line asserts the next line;
// the node is the first one in forEachChild's pre-order that starts on the asserted line, an expression statement
// unwrapped, a variable statement of one initialized declaration its initializer.
function expectTypes(dir, program) {
    const checker = program.getTypeChecker();
    const root = path.resolve(dir) + path.sep;
    const out = [];
    for (const file of program.getSourceFiles()) {
        if (!path.resolve(file.fileName).startsWith(root) || file.fileName.includes('/node_modules/')) continue;
        const text = file.text;
        if (!text.includes('$ExpectType')) continue;
        const lineStarts = file.getLineStarts();
        const assertions = new Map();
        const commentRegexp = /\/\/(.*)/g;
        let curLine = 0;
        for (let m; (m = commentRegexp.exec(text)) !== null;) {
            const match = /^ ?\$Expect(TypeSnapshot|Type|Error)( (.*))?$/.exec(m[1]);
            if (match === null || match[1] !== 'Type' || match[3] === undefined) continue;
            while (lineStarts[curLine + 1] <= m.index) curLine++;
            const line = /^\s*$/.test(text.slice(lineStarts[curLine], m.index)) ? curLine + 1 : curLine;
            if (!assertions.has(line)) assertions.set(line, match[3].trim());
        }
        const fileName = path.relative(process.cwd(), file.fileName);
        ts.forEachChild(file, function iterate(node) {
            if (node.kind === ts.SyntaxKind.EndOfFileToken) return;
            const start = node.getStart(file);
            const line = file.getLineAndCharacterOfPosition(start).line;
            const expected = assertions.get(line);
            if (expected !== undefined) {
                if (node.kind === ts.SyntaxKind.ExpressionStatement) node = node.expression;
                if (node.kind === ts.SyntaxKind.VariableStatement && node.declarationList.declarations.length === 1 &&
                    node.declarationList.declarations[0].initializer) {
                    node = node.declarationList.declarations[0].initializer;
                }
                const type = checker.getTypeAtLocation(node);
                const actual = type ? checker.typeToString(type, undefined, ts.TypeFormatFlags.NoTruncation) : '';
                const s = file.getLineAndCharacterOfPosition(node.getStart(file));
                const e = file.getLineAndCharacterOfPosition(node.end);
                out.push([fileName, s.line + 1, s.character + 1, e.line + 1, e.character + 1, ts.SyntaxKind[node.kind],
                    actual, expected]);
                assertions.delete(line);
            }
            ts.forEachChild(node, iterate);
        });
    }
    return out;
}

const rl = readline.createInterface({ input: process.stdin });
rl.on('line', line => {
    const dir = line.trim();
    if (!dir) return;
    let result;
    try {
        result = check(dir);
    } catch (e) {
        result = { project: dir, ms: 0, crash: String(e && e.stack || e).slice(0, 500), diags: [] };
    }
    process.stdout.write(JSON.stringify(result) + '\n');
});
