// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const kz = "z";
const v2 = { z: 1, get z() { return v2.z } };
const v3 = { [kz]: 1, get [kz]() { return v3.z } };
const v6 = { [kz]: 1, get z() { return v6.z } };
const w8 = { z: 1, set z(v: typeof w8) {} };
// the plain member last, or no plain member: the two agree
const v9 = { get z() { return v9.z }, z: 1 };
const v1 = { get z() { return v1.z } };
const sentinel: number = "";
