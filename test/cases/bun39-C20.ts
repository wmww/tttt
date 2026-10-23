// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "resolveJsonModule": true } }
// @filename: b.ts
import j from './j.json';
j;
const sentinel: number = "";
// @filename: j.json
{ "a": }
