// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "esModuleInterop": true, "module": "commonjs" } }
// @filename: a.ts
import * as ns from "./m";
import fn = require("./m");
declare function f<T>(p: typeof fn | T, q: T): T;
declare const c: typeof ns & { more: 1 };
f(ns, c);
const sentinel: number = "";
// @filename: m.d.ts
declare function fn(x: number): string;
declare namespace fn { const extra: number }
export = fn;
