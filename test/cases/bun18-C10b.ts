// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f3(...args: [...a: [number], b: string]): void;
let u3: typeof f3 = (p: string, q: string) => {};
const sentinel: number = "";
