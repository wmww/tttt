// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noImplicitAny": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { set p({ a, b } = { a: null, b: undefined }) { } };
const o2 = { set p([a, b] = [null, undefined]) { } };
const o3 = { set p({ a } = { a: [] }) { } };
class C1 { set p({ a, b } = { a: null, b: undefined }) { } }
const sentinel: number = "";
