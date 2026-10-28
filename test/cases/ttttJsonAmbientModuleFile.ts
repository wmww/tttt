// An ES module's import is JSON to Node.js by the file of the module it resolved to (tsc's isOnlyImportableAsDefault,
// GetSourceFileOfModule): an ambient module's is where it is declared, and an ambient module wins over the file a
// `paths` mapping finds (bun 54-L02c, 54-L02d)
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "module": "nodenext", "paths": { "x.json": ["./typed.json"] } } }
// @filename: amb.d.json.ts
declare module "z" { const v: { amb: number }; export default v; }
// @filename: amb.d.ts
declare module "x.json" { const v: { amb: number }; export default v; }
// @filename: typed.d.json.ts
declare const v: { k: number };
export default v;
// @filename: a.mts
import t1 from "z";
import { default as t2 } from "z";
t1.amb; t2.amb;
import t3 from "x.json";
t3.amb; t3.k;
