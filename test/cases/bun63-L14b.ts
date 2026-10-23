// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let r: 1; let x: any;
class P { a = 1; b = 2; }
declare const p: P;
({ a: x, ...r } = p);
({ ...r } = p);
enum E { A, B }
({ A: x, ...r } = E);
namespace NS { export const a = 1, b = 2; }
({ a: x, ...r } = NS);
function f() {} f.a = 1; f.b = 2;
({ a: x, ...r } = f);
interface I { a: number; b: number }
declare const i: I;
({ a: x, ...r } = i);
class Box<T> { a!: T; b!: T[]; }
declare const bx: Box<string>;
({ a: x, ...r } = bx);
({ a: x, ...r } = P);
declare const u: P | I;
({ a: x, ...r } = u);
const ce = class { a = 1; b = 2 };
({ a: x, ...r } = new ce());
type TL = { a: number; b: number };
declare const tl: TL;
({ a: x, ...r } = tl);
({ a: x, ...r } = { a: 1, b: 2 });
let need: { b: number; c: number };
({ a: x, ...need } = p);
for ({ a: x, ...r } of [p]) {}
const sentinel: number = "";
