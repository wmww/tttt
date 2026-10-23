// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E { Two = 2 }
interface AL extends ReadonlyArray<string | number> { length: E.Two }
declare function f<T extends AL>(x: T): T;
const r = f([1, "a"]);
const t: [number, string] = r;
const sentinel: number = "";
