#!/usr/bin/env node
// What tsc says an identifier's type is inside a real project (a corpus), where a reduced probe may not reproduce:
//   ./tsctypeat.js thirdparty/corpora/webpack/tsconfig.json lib/MultiCompiler.js 629 watchings
//   ./tsctypeat.js thirdparty/corpora/webpack/tsconfig.json lib/MultiCompiler.js '*' watching   (every line)
// The file is relative to the tsconfig's directory. Prints `line name : type` for each occurrence.
const path = require('path');
const ts = require(path.join(__dirname, 'thirdparty/tsc/lib/typescript.js'));
const [,, project, file, line, name] = process.argv;
if (!name) {
    console.error('usage: tsctypeat.js TSCONFIG FILE LINE|* IDENTIFIER');
    process.exit(2);
}
const config = ts.getParsedCommandLineOfConfigFile(path.resolve(project), {}, {
    ...ts.sys, onUnRecoverableConfigFileDiagnostic: d => console.error(ts.flattenDiagnosticMessageText(d.messageText, '\n')),
});
const program = ts.createProgram(config.fileNames, config.options);
const checker = program.getTypeChecker();
const source = program.getSourceFile(path.resolve(path.dirname(project), file));
if (!source) {
    console.error(`${file} isn't part of the program`);
    process.exit(1);
}
(function visit(node) {
    const at = source.getLineAndCharacterOfPosition(node.getStart(source)).line + 1;
    if ((line === '*' || at === +line) && ts.isIdentifier(node) && node.text === name) {
        console.log(at, name, ':', checker.typeToString(checker.getTypeAtLocation(node)));
    }
    ts.forEachChild(node, visit);
})(source);
