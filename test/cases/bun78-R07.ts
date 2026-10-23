// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function q1(a = await 1): void; function q1(a: any) {}
function q2(a = (await "s")): void; function q2(a: any) {}
function q3(a = await await true): void; function q3(a: any) {}
declare function q4(a = await 1): void; q4();
class K { m(a = await 1): void; m(a: any) {} }
// tsc reports these: the operand is not a literal, or the initializer is not the `await` itself
function r1(a = await -1): void; function r1(a: any) {}
function r2(a = [await 1]): void; function r2(a: any) {}
const sentinel: number = "";
