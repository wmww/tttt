// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let x: any;
let s: string = "";
declare const o: { "1n": number; a: number };
({ 1n: x } = {});
({ 1n: x } = [1, 2]);
({ 1n: s } = o);
({ 1n: { a: s } } = o);
({ 1n: s = 1 } = o);
for ({ 1n: s } of [o]) {}
class P { private "1n" = 1; }
({ 1n: s } = new P());
const sentinel: number = "";
