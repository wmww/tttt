// tsc 5.8's diagnostics for many projects in one process (./sweep.py's oracle, plan 44): reads project directories
// (each with a tsconfig.json) from stdin, one per line, and prints one JSON line per project:
// {"project", "ms", "diags": [[file, line, col, code], ...]} with paths relative to the working directory, line and
// col 1-based. The lib files' source files are shared between projects (parsing lib.dom.d.ts for every project
// would be most of the time); everything else is parsed per project, like `tsc -p DIR` would.
const ts = require(require('path').resolve(__dirname, '../thirdparty/tsc/lib/typescript.js'));
const path = require('path');
const readline = require('readline');

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
