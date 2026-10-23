// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(): void;
f<<>() => void>();
const q = f<<>() => void>;
f<new <>() => void>();
f<{ m<>(): void }>();
let ok: <>() => void;
const sentinel: number = "";
