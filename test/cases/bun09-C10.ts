// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const f1: ({ a }: { a: 1 } | undefined) => void;
const sentinel: number = "";
