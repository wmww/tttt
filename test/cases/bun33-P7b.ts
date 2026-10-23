// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X<A extends unknown[]> = [...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A];
const a = null! as X<X<X<X<[0]>>>>;
const sentinel: number = "";
