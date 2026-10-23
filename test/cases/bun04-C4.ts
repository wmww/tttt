// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
declare function dec(...a: any[]): any;
const a = 1;
export class B { m(@dec(a) x: number) { return x; } }
const sentinel: number = "";
