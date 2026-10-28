// @noUnusedParameters: true
// @noUnusedLocals: true
// @strict: true
// A parameter property outside a constructor implementation (TS2369) is a parameter: TS6133 when unused (tsc's
// isParameterPropertyDeclaration needs a constructor). Parameter decorators tsc doesn't check (no
// experimentalDecorators: TS1206) don't use the names they mention
export function f(private x: number) {}
export class C { m(public y: number) {} constructor(private z: number, public w: number) {} }
export const o = { m(readonly q: number) {} }
export class D { constructor(private z: number); constructor(z: any) {} }
declare function dec(...a: any[]): any;
const a = 1;
export class B { m(@dec(a, zzz) x: number) { return x; } }
export function g(@dec(a) y: number) { return y }
