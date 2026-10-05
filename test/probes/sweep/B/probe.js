const ts = require('/tmp/sweep-repro-B/ts-copy.js');
const p = ts.createProgram(['/tmp/sweep-repro-B/koa8.ts'], {strict:true, noEmit:true});
globalThis.__dbg=1; const c = p.getTypeChecker();
const sf = p.getSourceFile('/tmp/sweep-repro-B/koa8.ts');
ts.forEachChild(sf, n => { if (ts.isInterfaceDeclaration(n)) { const t = c.getTypeAtLocation(n.name); console.log(n.name.text, t.flags, t.objectFlags, c.getBaseTypes(t).map(b=>b.flags), c.getIndexInfosOfType(t).length);
  const u = c.getUnknownType(); console.log(' unknown->', c.isTypeAssignableTo(u, t), ' null->', c.isTypeAssignableTo(c.getNullType(), t)); } });
