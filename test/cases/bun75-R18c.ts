// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cb<T>(f: () => T): T;
const [a, b = () => 1] = cb(() => [1, () => 2]);
const sentinel: number = "";
