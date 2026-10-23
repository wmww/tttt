// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
export {};
type Keys = "a" | "b";
class C1 { [k: string]: any; [P in Keys]: number }
class C2 { static [k: string]: any; static [P in Keys]: number }
class C3 { [k: string]: any; [P in Keys]?: number }
class C4 { [k: string]: any; readonly [P in Keys]: number }
class C5 { [k: number]: any; [P in Keys]() {} }
class C6 { [k: symbol]: any; get [P in Keys]() { return 1 } }
const E7 = class { [k: string]: any; [P in Keys]: number };
declare class C8 { [k: string]: any; [P in Keys]: number }
abstract class C9 { [k: string]: any; abstract [P in Keys]: number }
const sentinel: number = "";
