// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A { abstract m(): void; }
declare function dec(...a: any[]): any;
declare function use(...a: any[]): any;
use(@dec class extends A {});
const sentinel: number = "";
