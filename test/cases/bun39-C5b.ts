// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export { A as A2 };
import { A2 as A3 } from './a';
class A extends A3 {}
const sentinel: number = "";
