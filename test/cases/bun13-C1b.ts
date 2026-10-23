// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Q = keyof M<D>;
type M<T> = { [K in keyof T]: T[K] };
interface D extends M<D> { m(): void }
const sentinel: number = "";
