const ts = require(process.env.TSJS||'/home/ai/tttt/thirdparty/tsc/lib/typescript.js');
const prog = ts.createProgram([process.argv[2]], { strict: true, noEmit: true });
const c = prog.getTypeChecker();
const sf = prog.getSourceFile(process.argv[2]);
function visit(n) { if (ts.isCallExpression(n)) { const s = c.getResolvedSignature(n); console.log(n.getText(), '=>', c.signatureToString(s)); } ts.forEachChild(n, visit); }
visit(sf);
