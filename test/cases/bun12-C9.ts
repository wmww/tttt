// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Array<T> { foo(): { [K in keyof this]: string }; }
const s: string[] = [1, 2, 3].foo();
const sentinel: number = "";
