// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const w1 = function (this: typeof w1, x = this) {};
const w3 = function (this: typeof w3, x: number, y = this) {};
const w7 = { m(this: typeof w7, x = this) {} };
const e2 = function (this: ReturnType<typeof e2>, x = this) { return x; };
// an ordinary first parameter, or a default that does not read `this` at once: the two agree
const w4 = function (a: typeof w4, x = a) {};
const w5 = function (this: typeof w5, x = () => this) {};
const sentinel: number = "";
