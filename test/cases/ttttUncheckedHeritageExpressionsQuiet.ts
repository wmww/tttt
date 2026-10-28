// @noUnusedLocals: true
// @noUnusedParameters: true
// @strict: true
// Heritage elements tsc never checks (an implemented expression that isn't a name, TS2500; an interface's, TS2499;
// the clauses after a grammar error, TS1172/TS1174/TS1175) report nothing: no unused declarations in them, no grammar
// errors from their bodies, and the names they mention aren't uses
export {};
declare class B {} interface I {}
export class C1 implements (function (p: any) { const u = 1; return 1; }) {}
export class C2 implements (class { private p = 1; m(q: any) { const u = 1; } }) {}
export interface J1 extends (function (p: any) { const u = 1; return this; }) {}
export class D1 extends B, (function (p: any) { const u = 1; return this; }) {}
export class D2 extends B extends (function (p: any) { const u = 1; return this; }) {}
export class D3 implements I implements (function (p: any) { const u = 1; return this; }) {}
export class C3 implements (function () { namespace X {} }) {}
