// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const anyV: any;
declare function pair<A>(a: A, b: A): [A, A];
// an omitted element is a plain `any`: a candidate for A beside `[any, any]`, so A is `any` in the return mapper and the literals are arrays
const [, [p1, q1]] = pair([1, "a"], [2, "b"]);
p1.toFixed();
let [, [p2, q2]] = pair([1, "a"], [2, "b"]);
q2 = 5;
// a default of type `any`
const [a3 = anyV, [p3, q3]] = pair([1, "a"], [2, "b"]);
p3.toFixed();
declare function both<A>(a: A): { p: A; q: A };
const { p: p4 = anyV, q: [r4, s4] } = both([1, "a"]);
r4.toFixed();
function f5([, [r, s]] = pair([1, "a"], [2, "b"])) { r.toFixed(); }
// only names: the two agree, the literals are tuples
const [a6, [p6, q6]] = pair([1, "a"], [2, "b"]);
p6.toFixed(); q6.toFixed();
const sentinel: number = "";
