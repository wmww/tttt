// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16", "paths": { "@data": ["./data.json"] } }, "files": ["a.ts"] }
// @filename: a.ts
import w1 = require("@data");
const sentinel: number = "";
// @filename: data.json
{"a":1}
