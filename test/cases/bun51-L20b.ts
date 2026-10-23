// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
export class C { private a = 1; m(c: C) { const { ...a: x } = c; return x; } }
export class D { private a = 1; private x = 2; m(c: D) { const { ...a: x } = c; return x; } }
export const sentinel: number = "";
