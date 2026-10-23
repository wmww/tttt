// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type G<A extends B[], B> = [A, B];
type E2<T> = T extends G<infer X, infer Y> ? X : never;
function fe<T>(y: E2<T>) { y.includes(1); }
const sentinel: number = "";
