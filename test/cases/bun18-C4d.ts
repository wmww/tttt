// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(...r: readonly [number, ...string[]]): void;
declare function tail<A extends unknown[]>(f: (x: number, ...a: A) => void): (...a: A) => void;
tail(f);
const sentinel: number = "";
