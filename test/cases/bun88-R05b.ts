// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
export {};
function f1() {
    const kk = "x"; const oo = {};
    class D1 { [k: string]: any; [kk in oo]: number = 1 }
    return D1;
}
function f2() {
    const kk = "x"; const oo = {};
    class D2 { [kk in oo]: number = 1 }
    return D2;
}
class D3 { [k: string]: any; [later in laterObj]: number = 1 }
const later = "x"; const laterObj = {};
class D4 { [k: string]: any; [missing1.a.b in missing2()]: number = 1 }
class D5 { [k: string]: any; [(missing3) in [missing4]]: number = 1 }
class D6 { [k: string]: any; [this.x in super.y]: number = 1 }
class D7<T> { [k: string]: any; [T in T]: number = 1 }
function f8(this: any, arg: string) { return class { [k: string]: any; [arg in this]: number = 1 } }
import { gone } from "./gone";
class D9 { [k: string]: any; [gone in gone]: number = 1 }
const sentinel: number = "";
