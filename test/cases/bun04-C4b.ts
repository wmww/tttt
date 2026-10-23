// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function dec(...a: any[]): any;
export class I { @dec [k: string]: any; }
const sentinel: number = "";
