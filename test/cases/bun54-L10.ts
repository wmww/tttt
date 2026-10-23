// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { get a() { return 1 }, a: 2 };
o1.a();
const o2 = { a: 2, get a() { return 1 } };
o2.a();
const o3 = { set a(v: number) {}, get a() { return 1 } };
o3.a();
const o4 = { get a() { return 1 }, set a(v: number) {} };
o4.a();
const o5 = { get a() { return 1 }, a() {} , b: 1 };
o5.b();
const o6 = { set a(v: number) {}, a: 2 };
o6.a();
const k = "a";
const o7 = { get [k]() { return 1 }, [k]: 2 };
o7.a();
const o8 = { [k]: 2, get a() { return 1 } };
o8.a();
const o9 = { get a() { return 1 }, [k]: 2 };
o9.a();
const sentinel: number = "";
