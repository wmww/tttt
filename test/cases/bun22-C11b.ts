// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(a: T, b: [x: T]): T;
declare const a2: { m(x: string): void };
declare const t2: [x: { m(x: string | number): void }];
f(a2, t2).m(1);
const sentinel: number = "";
