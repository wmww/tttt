// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "noEmit": true, "types": [], "lib": ["es5"], "target": "es2022" } }
// @filename: a.ts
export async function e1() {}
export class C { async m() {} }
async function local() {}
export { local };
// not exported: not asked by the declaration emitter either
async function hidden() {}
const sentinel: number = "";
