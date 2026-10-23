// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const kz = "z";
const w10 = { set z(v) {}, get [kz]() { return w10.z } };
const w11 = { get [kz]() { return w11.z }, set z(v) {} };
const w15 = { get [kz](): typeof w15.z { return 1 }, set z(v) {} };
// a class, or the getter with the plain name: the two agree
const w12 = { set [kz](v) {}, get z() { return w12.z } };
class K1 { set z(v) {} get [kz]() { return this.z } }
const sentinel: number = "";
