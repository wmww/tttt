// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let t: (object & { length?: string }) | string = { length: 5 };
const sentinel: number = "";
