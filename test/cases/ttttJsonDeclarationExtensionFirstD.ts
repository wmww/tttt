// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext", "allowArbitraryExtensions": true } }
// @filename: package.json
{ "type": "module" }
// @filename: a.d.b.d.json.ts
export declare const named: 1;
// @filename: c.d.json.ts
export declare const named: 1;
// @filename: a.ts
// A declaration file is a JSON module's by tsgo's DeclarationFileExtension, which starts at the base name's first
// `.d.`: `a.d.b.d.json.ts`'s is `.d.b.d.json.ts`, no JSON (no TS1544: bun 42-C8); `c.d.json.ts`'s is
import { named } from "./a.d.b.json" with { type: "json" };
import { named as n2 } from "./c.json" with { type: "json" };
