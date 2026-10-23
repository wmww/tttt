// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function call<T>(f: (x: T) => void, v: T): T;
call(({ a } = { a: 1 }) => {}, { a: 2 });
call(([a] = [1]) => {}, [2]);
call(({ a } = { a: 1, z: 1 }) => {}, { a: 2, y: 3 });
call((x = { a: 1 }) => {}, { a: 2 });
declare function call3<T>(o: { f: (x: T) => void, v: T }): T;
call3({ f({ a } = { a: 1 }) {}, v: { a: 2 } });
declare function call6<T>(f: (x: { p: T }) => void, v: T): T;
call6(({ p } = { p: 1 }) => {}, 2);
[{ a: 1 }].map(({ a } = { a: 1 }) => a);
[1].reduce(({ n } = { n: 0 }, x) => ({ n: n + x }), { n: 0 });
const sentinel: number = "";
