// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M12 = { [K in keyof M12 as K]: number };
const sentinel: number = "";
