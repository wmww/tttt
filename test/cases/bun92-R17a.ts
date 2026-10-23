// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Cond<T> = T extends Partial<A> ? { y: 1 } : { z: 1 };
interface A extends Cond<A> { x: number }
const sentinel: number = "";
