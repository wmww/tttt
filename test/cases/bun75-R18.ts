// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cb<T>(f: () => T): T;
const [a1, b1 = () => a1] = [1, () => 2];
const { a2, b2 = () => a2 } = { a2: 1, b2: () => 2 };
const [a3, b3 = () => a3] = cb(() => [1, () => 2]);
function f4([a4, b4 = () => a4] = [1, () => 2]) {}
const [a5, b5 = { m: () => a5 }] = [1, { m: () => 2 }];
const sentinel: number = "";
