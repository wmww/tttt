// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o = { m<A>(a: A) { class L { a!: A } return new L(); } };
const v: 1 = o.m(1);
const sentinel: number = "";
