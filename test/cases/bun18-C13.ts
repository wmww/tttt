// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let p1: (this: string, { a }: { a: number }) => void;
declare let p2: (this: string, x: { a: string }) => void;
p1 = p2;
const sentinel: number = "";
