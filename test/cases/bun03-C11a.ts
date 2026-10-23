// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class P { private priv = 1 }
declare function fc({ priv: z }: P): void;
const sentinel: number = "";
