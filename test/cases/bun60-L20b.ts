// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() { const s = Symbol(); return { [s]: 1 }; }
const k = "foo";
export class H1 { get foo() { return f(); } set [k](v) {} }
export class H2 { get [k]() { return f(); } set foo(v) {} }
export class H3 { get [k]() { return f(); } set [k](v) {} }
export class H4 { set [k](v) {} get foo() { return f(); } }
export class H5 { static get foo() { return f(); } static set [k](v) {} }
const sentinel: number = "";
