// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import D from "./a";
declare const dec: any;
declare class Base { }
interface Base extends D {}
@dec export default class extends Base {}
const sentinel: number = "";
// @filename: b.ts
import D from "./b";
declare const dec: any;
@dec export default class extends D {}
// @filename: c.ts
declare const dec: any;
@dec export default class { a = 1 }
// @filename: d.ts
import "./c";
declare module "./c" { export default class { b: number } }
