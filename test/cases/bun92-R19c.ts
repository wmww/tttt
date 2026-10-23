// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"module":"preserve","moduleResolution":"bundler" } }
// @filename: a.ts
import * as m1 from "./m1";
type S<X> = [Extract<keyof X, number>, Extract<keyof X, string>];
const t1: S<typeof m1> = ["?", -99];
type M = { [K in keyof typeof m1]: K }; const m: M = 0;
const sentinel: number = "";
// @filename: m1.ts
const x = 1; export { x as "0", x as "1.5", x as "a b", x as y };
export * as "2" from "./m2";
// @filename: m2.ts
export const z = 1;
