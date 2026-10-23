// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: b.cts
import * as u3 from "./u3.cjs"; u3.a; u3.default;
const sentinel: number = "";
// @filename: u3.d.cts
declare const u: { default: number; a: 1 } | { default: string; a: 2 }; export = u;
