// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function dec(...a: any[]): any;
@dec(() => {}) declare class C1 {}
@dec(function () {}) declare class C2 {}
@dec({ m() {} }) declare class C3 {}
@dec(class { m() {} }) declare class C4 {}
@dec(() => {}) export declare class C5 {}
export @dec(() => {}) declare class C6 {}
@dec(() => {}) declare abstract class C8 {}
@dec(() => { let x = 1; return x; }) declare class C9 {}
const sentinel: number = "";
