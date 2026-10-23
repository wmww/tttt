// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type AB = { a: 1 } | { b: 2 };
function f2<T extends AB>(t: T, { ...r } = t) { const x: T = r; }
const sentinel: number = "";
