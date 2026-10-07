// Usage: node noexpect.js types/NAME file:line...  -- tsc diagnostics on the lines following the given @ts-expect-error lines, with directives disabled
const ts = require('/home/ai/tttt/thirdparty/tsc/built/local/typescript.js');
const path = require('path');
const [proj, ...locs] = process.argv.slice(2);
const cfgPath = path.resolve(proj, 'tsconfig.json');
const cfg = ts.getParsedCommandLineOfConfigFile(cfgPath, {}, { ...ts.sys, onUnRecoverableConfigFileDiagnostic: d => console.log(d) });
const host = ts.createCompilerHost(cfg.options);
const orig = host.getSourceFile;
host.getSourceFile = (f, lv, ...r) => {
  const text = ts.sys.readFile(f);
  if (text === undefined) return undefined;
  return ts.createSourceFile(f, text.replace(/@ts-expect-error/g, '@ts-expect-errox'), lv);
};
const prog = ts.createProgram({ rootNames: cfg.fileNames, options: cfg.options, host });
const diags = ts.getPreEmitDiagnostics(prog);
const want = locs.map(l => { const [f, n] = l.split(':'); return [path.resolve(f), +n]; });
for (const d of diags) {
  if (!d.file) continue;
  const { line } = d.file.getLineAndCharacterOfPosition(d.start);
  for (const [f, n] of want) if (path.resolve(d.file.fileName) === f && line + 1 >= n + 1 && line + 1 <= n + 3)
    console.log(`${path.relative(process.cwd(), d.file.fileName)}:${line + 1} TS${d.code}: ${ts.flattenDiagnosticMessageText(d.messageText, '\n  ').slice(0, 400)}`);
}
