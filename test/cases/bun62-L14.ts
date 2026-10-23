// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const u: unknown; declare const a: {}; declare const c: boolean;
let e = {}; const { x, ...rest2 } = { x: 1 };
type I = { [k: string]: number };
if (u) {
  const t1: I = c ? u : { ...a };
  const t2: I = c ? { ...a } : u;
  const t3: I[] = [u, { ...a }];
  const t4: I = c ? u : { ...{} };
  const t5: I = c ? u : { ...a, ...a };
  const t6: I = c ? u : { ...e };
  const t7: I = c ? u : { ...rest2 };
}
const sentinel: number = "";
