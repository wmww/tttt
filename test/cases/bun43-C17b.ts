// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A { abstract m(): void; }
declare function dec(...a: any[]): any;
@dec export default class extends A {}
export const sentinel: number = "";
