// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["m.ts", "d.ts", "e.ts", "use.ts"] }
// @filename: d.ts
export {};
{ declare module "./m" { interface InBlock { a: 1 } } }
declare module "./m" { interface AfterBlock { a: 1 } }
// @filename: e.ts
export {};
declare module "./m" { interface BeforeBlock { a: 1 } }
if (1) { declare module "./m" { interface InBlock2 { a: 1 } } }
// @filename: m.ts
export interface Own { o: 1 }
// @filename: use.ts
import { Own, InBlock, AfterBlock, BeforeBlock, InBlock2 } from "./m";
const sentinel: number = "";
