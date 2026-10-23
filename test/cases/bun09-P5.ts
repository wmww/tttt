// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { p: 0 }
type X1 = I & { a: 1 };
function f1<T extends X1>(t: T) { const q: { a: 2 } = t; }
const sentinel: number = "";
