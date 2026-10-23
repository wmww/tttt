// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
export {};
type Ctor<T = {}> = new (...a: any[]) => T;
function g4<T extends Ctor, U extends { s: string }>(B: T & U) { return class extends B { static s = 1; }; }
function g5<T extends Ctor>(B: T & { s: string; f(): void }) { return class extends B { static s = 1; static f = 1; }; }
function g7<T extends Ctor>(B: T & { [k: string]: number }) { return class extends B { static s = "x"; }; }
function h14<T extends Ctor, K extends string>(B: T & Record<K, number>) { return class extends B {}; }
const sentinel: number = "";
