// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function check<T>(x: T): asserts x;
export function h(p: {}) { check(k()); return p; }
export function k() { return h({}); }
const sentinel: number = "";
