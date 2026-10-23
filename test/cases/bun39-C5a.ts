// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { A as A2 } from './a';
export class A extends A2 {}
const sentinel: number = "";
