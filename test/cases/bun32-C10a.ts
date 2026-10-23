// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
({ abstract e() {} });
({ readonly m() {} });
({ async public m() {} });
({ public public m() {} });
const sentinel: number = "";
