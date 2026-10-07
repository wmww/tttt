#!/usr/bin/env node
// What tsc's TS2589 counter (instantiationCount, reset by every checkExpression/checkSourceElement) reaches per node
// of a file inside a project: the budget tttt has to stay under where tsc doesn't report TS2589.
//   ./tscinstcount.js thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json type-tests/pg/select.ts [N]
//   VARIANCES=1 ./tscinstcount.js ...   also prints each variance measurement: symbol, type parameter, total
//                                       instantiations and new types it took, and the variance (tsc's flags:
//                                       8 Unmeasurable, 16 Unreliable)
// The file is relative to the tsconfig's directory. Prints the N (default 20) nodes with the highest count, and the
// program's totals. It patches a copy of thirdparty/tsc/built/local/typescript.js in memory (the counters are closure
// variables of createTypeChecker), so it breaks if tsc's emitted code changes shape: it checks every patch applied.
const fs = require('fs');
const path = require('path');
const Module = require('module');
const [,, project, file, top] = process.argv;
if (!file) {
    console.error('usage: tscinstcount.js TSCONFIG FILE [N]');
    process.exit(2);
}
const tsPath = path.join(__dirname, 'thirdparty/tsc/built/local/typescript.js');
let src = fs.readFileSync(tsPath, 'utf8');
function patch(re, replacement) {
    const next = src.replace(re, replacement);
    if (next === src) throw new Error('tscinstcount.js: patch did not apply: ' + re);
    src = next;
}
// Each increment remembers the highest count the current node reached
patch(/(\n\s*)instantiationCount\+\+;/,
    '$1instantiationCount++; if (currentNode) { const m = globalThis.__icByNode; if ((m.get(currentNode) || 0) < instantiationCount) m.set(currentNode, instantiationCount); }');
if (process.env.VARIANCES) {
    patch(/function getVariancesWorker\(([^)]*)\) \{/, 'function getVariancesWorker($1) { let __ic0 = 0, __tc0 = 0;');
    patch(/(\n\s*)variance = \(isTypeAssignableTo\(typeWithSub, typeWithSuper\)/,
        '$1__ic0 = totalInstantiationCount, __tc0 = typeCount;$1variance = (isTypeAssignableTo(typeWithSub, typeWithSuper)');
    patch(/(\n\s*)variances\.push\(variance\);/,
        '$1globalThis.__variances.push([symbol.escapedName, tp.symbol.escapedName, totalInstantiationCount - __ic0, typeCount - __tc0, variance]);$1variances.push(variance);');
}
globalThis.__icByNode = new Map();
globalThis.__variances = [];
const m = new Module(tsPath, module);
m.filename = tsPath;
m.paths = Module._nodeModulePaths(path.dirname(tsPath));
m._compile(src, tsPath);
const ts = m.exports;

const config = ts.getParsedCommandLineOfConfigFile(path.resolve(project), {}, {
    ...ts.sys, onUnRecoverableConfigFileDiagnostic: d => console.error(ts.flattenDiagnosticMessageText(d.messageText, '\n')),
});
const program = ts.createProgram(config.fileNames, config.options);
const target = path.resolve(path.dirname(project), file);
const source = program.getSourceFile(target);
if (!source) {
    console.error(`${file} isn't part of the program`);
    process.exit(1);
}
// The whole program, in tsc's order, so that what other files compute first (variances, relations) isn't charged to
// this one, as in a real run
const diagnostics = program.getSemanticDiagnostics();
const checker = program.getTypeChecker();
for (const d of diagnostics) {
    if (d.file === source && d.code === 2589) console.log('TS2589 at line', source.getLineAndCharacterOfPosition(d.start).line + 1);
}
const rows = [];
for (const [node, count] of globalThis.__icByNode) {
    if (node.getSourceFile() !== source) continue;
    const line = source.getLineAndCharacterOfPosition(node.getStart(source)).line + 1;
    rows.push([count, line, ts.SyntaxKind[node.kind], node.getText(source).slice(0, 70).replace(/\s+/g, ' ')]);
}
rows.sort((a, b) => b[0] - a[0]);
console.log('count\tline\tkind\ttext');
for (const r of rows.slice(0, +(top || 20))) console.log(r.join('\t'));
console.log(`program: ${checker.getInstantiationCount()} instantiations, ${checker.getTypeCount()} types`);
for (const v of globalThis.__variances) console.log('variance', v.join(' '));
