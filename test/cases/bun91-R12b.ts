// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "noUnusedLocals": true, "noUnusedParameters": true } }
// @filename: a.ts
declare const o: any;
with (o) { var a1 = 1; }
with (o) { var [a2, ...a3] = o; }
with (o) { namespace a4 {} }
function f() { with (o) { var b1 = 1; let b2 = 2; } }
f();
export const sentinel: number = "";
