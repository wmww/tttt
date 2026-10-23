// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare let on: { n: number; m(): void };
({ m() {} } = on);
const sentinel: number = "";
