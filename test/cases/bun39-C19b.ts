// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { Base as B1 } from './m1';
import { Base as B2 } from './m2';
class C implements B1, B2 { x = { y: "" } }
const sentinel: number = "";
// @filename: m1.ts
export interface Base { x: number }
// @filename: m2.ts
export interface Base { x: { y: number } }
