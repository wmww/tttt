// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "module": "commonjs", "types": [], "lib": ["es2022"] } }
// @filename: m.js
Object.defineProperty(exports, "p6", { value: undefined });
Object.defineProperty(exports, "p7", { get() { return null; } });
// @filename: u.ts
import m = require("./m");
declare let n: never;
n = m.p6; n = m.p7;
const sentinel: number = "";
