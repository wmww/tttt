// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "noUnusedLocals": true, "noUnusedParameters": true, "noImplicitReturns": true, "allowUnreachableCode": false, "allowUnusedLabels": false, "noFallthroughCasesInSwitch": true } }
// @filename: a.ts
export {};
declare class B {} interface I {}
export class C1 implements (function () { const a: number = ""; return a; }) {}
export class C7 implements (function () { return this; }) {}
export class C9 implements (function (p: any) {}) {}
export class C10 implements (function () { const u = 1; }) {}
export class C12 implements (class { private p = 1; m(q: any) { const u = 1; } }) {}
export class C13 implements ((p: any) => { const u = 1; }) {}
export class C14 implements ({ m(p: any) { const u = 1; return this; } }) {}
export class C15 implements (function <T>() {}) {}
export class C19 implements (function ({ a, b }) {}) {}
export class C20 implements (function (...r) {}) {}
export interface J1 extends (function (p: any) { const u = 1; return this; }) {}
export class D1 extends B, (function (p: any) { const u = 1; return this; }) {}
export class D2 extends B extends (function (p: any) { const u = 1; return this; }) {}
export class D3 implements I implements (function (p: any) { const u = 1; return this; }) {}
export interface J2 extends I extends (function (p: any) { const u = 1; return this; }) {}
export class C22 implements (function () { type U = 1; interface V {} enum W {} namespace X {} class Y {} }) {}
const sentinel: number = ""; sentinel;
