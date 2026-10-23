// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export async function a() { namespace N { var await = 1; } }
const sentinel: number = "";
