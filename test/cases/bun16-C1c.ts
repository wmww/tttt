// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function mk<T>(x: T): new () => {};
class C extends mk(x) {}
declare const y: C | undefined;
const x = y!;
const sentinel: number = "";
