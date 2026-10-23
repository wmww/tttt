// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noImplicitAny": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { set p({ a = null, b = undefined, c = [] }) { } };
class C1 { set p({ a = null, b = undefined, c = [] }) { } }
const sentinel: number = "";
