// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
export {};
type Ctor<T = {}> = new (...a: any[]) => T;
function h1<T extends Ctor, U extends { s: string }>(B: T & U) { return class extends B { static s = "ok"; }; }
function h2<T extends Ctor, U extends { s: string }>(B: T & U) { return class extends B {}; }
function h5<T extends Ctor, U extends object>(B: T & U) { return class extends B {}; }
function h7<T extends Ctor, U extends Ctor>(B: T & U) { return class extends B {}; }
function h9<U extends { s: string }, T extends Ctor>(B: U & T) { return class extends B {}; }
const sentinel: number = "";
