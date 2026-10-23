// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X<A extends unknown[]> = [...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A];
const g7: { m: F7 } = { m() { return 1; } };
const g8: { a: 1, m: F8 } = { a: 1, async m() { return 1; } };
let g9: { m: F9 };
g9 = { ["m"]() { return 1; } };
take({ m() { return 1; } });
declare function take(o: { m: F10 }): void;
type F7 = () => X<X<X<X<[7]>>>>;
type F8 = () => X<X<X<X<[8]>>>>;
type F9 = () => X<X<X<X<[9]>>>>;
type F10 = () => X<X<X<X<[10]>>>>;
const sentinel: number = "";
