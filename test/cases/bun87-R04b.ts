// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function sp0<T>(t: T) { return { ...{} }; }
function rs0<T>(t: T) { const { a, ...r } = { a: 1 }; return r; }
const c0 = sp0(1); declare let x0: string & typeof c0; const y0: 0 = x0;
const c3 = rs0(1); declare let x3: string & typeof c3; const y3: 0 = x3;
const sentinel: number = "";
