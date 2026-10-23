// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g3<T>(g: (t: T) => void, f: () => Generator<(x: number) => void, T>): T;
g3(t => { t.toFixed(); }, function* <U>() { yield (x) => x; return 1; });
const sentinel: number = "";
