// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "isolatedModules": true, "experimentalDecorators": true, "emitDecoratorMetadata": true, "module": "esnext" } }
// @filename: a.ts
import { A } from "./m";
declare const dec: any;
const k = "x";
class C { @dec get [k]() { return null! } set [k](v: A) {} }
const sentinel: number = "";
// @filename: m.ts
export interface A {}
