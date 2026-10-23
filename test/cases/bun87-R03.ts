// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h1({ a = 1 }) {}
function h2({ a = 1 }) {}
function f1({}) {}
function f2({}) {}
type H1 = Parameters<typeof h1>[0]; type H2 = Parameters<typeof h2>[0];
type F1 = Parameters<typeof f1>[0]; type F2 = Parameters<typeof f2>[0];
declare let u1: H1 | H2; const e1: 0 = u1;
declare let u2: F1 | F2; const e2: 0 = u2;
declare let u3: H1 & H2; const e3: 0 = u3;
declare let u4: F1 & F2; const e4: 0 = u4;
declare const o: { a?: number; z: 1 }; const { z, ...r1 } = o; const { z: zz, ...r2 } = o;
declare let u5: typeof r1 | typeof r2; const e5: 0 = u5;
declare const p: { z: 1 }; const { z: z3, ...r3 } = p; const { z: z4, ...r4 } = p;
declare let u6: typeof r3 | typeof r4; const e6: 0 = u6;
declare let u7: typeof r3 & typeof r4; const e7: 0 = u7;
const s1 = { ...r3 }, s2 = { ...r4 };
declare let u8: typeof s1 | typeof s2; const e8: 0 = u8;
const l1 = {}, l2 = {};
declare let u9: typeof l1 | typeof l2; const e9: 0 = u9;
function g({ a = 1 }, { a: b = 1 }) {}
declare let u10: Parameters<typeof g>[number]; const e10: 0 = u10;
const [{ k = 1 }, { k: k2 = 1 }] = null! as any[];
const sentinel: number = "";
