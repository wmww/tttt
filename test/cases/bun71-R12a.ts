// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "target": "es2022" } }
// @filename: a.ts
export {};
declare function dec(...x: any[]): (c: any, ctx: any) => void;
declare const o: any;
const e1 = @dec(#i in o) class { #i = 1; };
const e2 = @dec(o.#i) class { #i = 1; };
const e3 = @dec(() => o.#i) class { #i = 1; };
const e4 = @dec((x: E4) => x.#i) class E4 { #i = 1; };
const e5 = @dec(() => e5.#s) class { static #s = 1; };
const sentinel: number = "";
