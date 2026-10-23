// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function tk(f: (a: number) => Generator<(x: number) => number>): void;
tk(function* <T>(a) { yield (x) => x; });
const sentinel: number = "";
