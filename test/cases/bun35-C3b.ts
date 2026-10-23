// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16", "paths": { "j": ["./d.json"] } } }
// @filename: a.ts
import j = require("j");
const sentinel: number = "";
// @filename: d.json
{}
