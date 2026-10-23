// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { a: 2, get a(): number { return 1 }, set a(v: string | number) {} };
o1.a = "s";
const o2 = { get a(): number { return 1 }, a: 2, set a(v: string | number) {} };
o2.a = "s";
const o3 = { get a(): number { return 1 }, set a(v: string | number) {}, a: 2 };
o3.a = "s";
const o4 = { get a(): number { return 1 }, set a(v: string | number) {} };
o4.a = "s";
const p1: boolean = o1;
const p2: boolean = o2;
const p3: boolean = o3;
const p4: boolean = o4;
const k = "a";
const o5 = { [k]: 2, get a(): number { return 1 }, set a(v: string | number) {} };
o5.a = "s";
const p5: boolean = o5;
const o6 = { a: 2, get [k](): number { return 1 }, set [k](v: string | number) {} };
o6.a = "s";
const p6: boolean = o6;
({ a: o1.a } = { a: "s" });
for (o1.a of ["s"]) {}
o1.a += "s";
const sentinel: number = "";
