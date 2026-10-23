// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X7 = [1] & { a: 1 };
function f7<T extends X7>(t: T) { const q: { a: 2 } = t; }
const sentinel: number = "";
