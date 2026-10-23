// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs", "esModuleInterop": true } }
// @filename: a.ts
import { a as a1 } from "./m1";
const r1: boolean = a1;
const sentinel: number = "";
// @filename: m1.ts
declare const v: { a: 1 } | { [k: string]: 2 };
export = v;
