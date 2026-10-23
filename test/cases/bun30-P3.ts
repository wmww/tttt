// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(): { b: { d: number } };
declare function check<T>(x: T): asserts x;
export function h() { check(k()); return (f().b = f().b).d; }
export function k() { return h(); }
const sentinel: number = "";
