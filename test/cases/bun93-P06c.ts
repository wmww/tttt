// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { export abstract class { m() {} } export class {} }
N.x;
const sentinel: number = "";
