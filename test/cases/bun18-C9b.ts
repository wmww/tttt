// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let p8: (...{ length }: [number, number]) => void;
declare let p4: (a: number, b: string) => void;
p8 = p4;
const sentinel: number = "";
