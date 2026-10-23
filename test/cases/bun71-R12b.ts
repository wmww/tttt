// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "target": "es2022" } }
// @filename: a.ts
export {};
declare function dec(...x: any[]): (c: any, ctx: any) => void;
class Out { #i = "outer"; m(x: Out) { return @dec(x.#i.length, #i in x) class { #i = 1; }; } }
const sentinel: number = "";
