// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: any;
interface G<T extends string> {}
declare function g<T extends string>(): void;
declare class K<T extends string> {}
type X1 = G<(number | boolean)>;
type X2 = G<(() => void)>;
g<(number)>();
new K<(number)>();
class L extends K<(number)> {}
interface M extends G<(number)> {}
type Y1 = { a: 1 }[("b" | "c")];
type Y2 = { a: 1 }[(number)];
type Y3 = { a: 1 }[(boolean)];
type Y4 = [1, 2][(5)];
type M1 = { [K in (boolean)]: 1 };
type M2 = { [K in "a" as (boolean)]: 1 };
function f1(): (ReturnType<typeof f1>) { return o }
const sentinel: number = "";
