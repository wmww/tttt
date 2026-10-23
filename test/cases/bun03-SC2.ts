// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "skipLibCheck": true } }
// @filename: a.d.ts
declare enum E { A }
// @filename: b.ts
enum E { B }
const sentinel: number = "";
