// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const s1 = { set a(v: typeof s1.a) {} };
const r1: null = s1;
const s7 = { set a(v: typeof s7.b) {}, set b(v: typeof s7.a) {} };
const r7: null = s7;
const s31 = { get a(): typeof s31.a { return 1 } };
const r31: null = s31;
const sentinel: number = "";
