// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function a1<T extends string | undefined>(x: T) { return x!.toUpperCase(); }
function a2<T extends number | null>(x: T) { return x!.toFixed(); }
function a3<T extends string | undefined>(o: { p: T }) { return o.p!.length; }
function a4<T extends string | undefined>(f: () => NonNullable<T>) { return f().length; }
class A5<T extends string | undefined> { p!: T; m() { return this.p!.length; } }
function a6<T extends string | undefined>(x: T) { x!.length = 1; }
// the two agree: a constraint that is still a union or an object type after `& {}`, a reference in a constraint position
function b1<T extends string | number | undefined>(x: T) { return x!.toString(); }
function b2<T extends { a: number } | undefined>(x: T) { return x!.a; }
function b3<T extends string | undefined>(x: NonNullable<T>) { return x.length; }
function b4<T extends string | undefined>(x: T) { return x![0]; }
const sentinel: number = "";
