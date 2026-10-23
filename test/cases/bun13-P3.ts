// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function m<T>(a: T, b: T, c: (t: T) => void): T;
m([[[[[[[[[(x) => 1]]]]]]]]], [[[[[[[[[1]]]]]]]]], (t) => {});
const sentinel: number = "";
