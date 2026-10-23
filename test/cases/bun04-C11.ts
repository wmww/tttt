// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export type A1<T = U<number>, U = 1> = [T, U];
const sentinel: number = "";
