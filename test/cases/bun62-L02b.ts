// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X<A extends unknown[]> = [...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A];
take1(function named(x) {});
take2((x) => {});
declare let o: I3;
o.m = function named3(x) {};
o.n = function (x) {};
o.k = (x) => {};
const o8: { m: F8 } = { m: function (x) {} };
declare function take1(cb: (x: X<X<X<X<[1]>>>>) => void): void;
declare function take2(cb: (x: X<X<X<X<[2]>>>>) => void): void;
interface I3 { m(x: X<X<X<X<[3]>>>>): void; n(x: X<X<X<X<[4]>>>>): void; k(x: X<X<X<X<[5]>>>>): void }
type F8 = (x: X<X<X<X<[8]>>>>) => void;
const sentinel: number = "";
