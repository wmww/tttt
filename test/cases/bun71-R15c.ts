// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
export {};
type Ctor<T = {}> = new (...a: any[]) => T;
declare const key: string;
function i1<T extends Ctor & { [k: string]: number }>(B: T) { return class extends B { static s = "x"; }; }
function i2<T extends Ctor>(B: T) { return class extends B { static [k: string]: number; static s = "x"; }; }
function i3<T extends Ctor>(B: T) { return class extends B { static [k: string]: number; static [key] = "x"; }; }
function i4<T extends Ctor>(B: T) { return class extends B { static [k: string]: number; static [k: number]: string; }; }
function i7<T extends Ctor & { [k: string]: number }>(B: T) { return class extends B { static s = 1; static m() {} }; }
const sentinel: number = "";
