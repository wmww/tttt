// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function entries<T>(t: T) {
  if (t === null || t === undefined) return;
  for (const [k, v] of Object.entries(t)) v.foo;
}
function record<T>(t: T) {
  if (t === undefined || t === null) return;
  const r: Record<string, unknown> = t;
}
function both<T>(t: T) { if (t === null) throw 0; if (t === undefined) throw 0; return t; }
const y = both(null! as unknown);
const r: { [k: string]: number } = y;
const { ...rest } = {}; const stays1: { [k: string]: number } = rest;
const spread = { ...{} }; const stays2: { [k: string]: number } = spread;
const sentinel: number = "";
