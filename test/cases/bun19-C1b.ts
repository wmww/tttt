// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type C1<T> = T extends T[] ? number : number;
function g<T>(x: number, y: C1<T> = x) {}
const sentinel: number = "";
