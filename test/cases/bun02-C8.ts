// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function s1(x: string): void; function s1(x: any) {} var q1; function s1(y: number, z: number): number { return 1 }
const sentinel: number = "";
