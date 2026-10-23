// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext", "allowArbitraryExtensions": true } }
// @filename: a.d.b.d.json.ts
export declare const named: 1;
// @filename: a.ts
import { named } from "./a.d.b.json" with { type: "json" };
const sentinel: number = "";
// @filename: package.json
{ "type": "module" }
