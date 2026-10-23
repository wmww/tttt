// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h1(this: ReturnType<typeof h1>, x = this) { return 1; }
function h2(this: Parameters<typeof h2>[0], x = this) {}
function h3(this: ThisParameterType<typeof h3>, x = [this]) {}
function h8(this: ReturnType<typeof h8>, x: typeof this) { return 1; }
class K1 { m(this: ReturnType<K1["m"]>, x = this) { return x; } }
// an ordinary first parameter: the two agree
function p1(a: ReturnType<typeof p1>, x = a) { return x; }
const sentinel: number = "";
