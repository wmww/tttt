// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { X } from "./b";
module "./b" { export interface X { y: number } }
declare const x: X;
x.y;
const sentinel: number = "";
// @filename: b.ts
export interface X { x: number }
