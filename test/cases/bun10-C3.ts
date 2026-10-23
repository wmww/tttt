// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { z: 1 };
const s2 = { get h() { return s2; }, ...o };
const sentinel: number = "";
