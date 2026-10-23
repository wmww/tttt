// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
declare function dec(...a: any[]): any;
const c1 = @dec abstract class { abstract m(): void };
const c2 = @dec public class {};
const c3 = (@dec export default class {});
new c1();
const sentinel: number = "";
