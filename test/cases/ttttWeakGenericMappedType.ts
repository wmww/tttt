// A generic mapped type is weak by the properties its modifiers type gives it (tsc's isWeakType over
// resolveStructuredTypeMembers): `Partial<T>` with `T extends { a: A }` gets TS2559
function f<T extends { a: string }>(x: Partial<T>, y: { b: number }) {
  x = y;
}
declare const oz: { z: 1 };
function g<T extends { a: number }>() { const x1: Partial<T> = oz; const x2: Partial<T> = 1; const x3: Partial<T> = {}; }
function h<T>(x: Partial<T>, y: { b: number }) { x = y; }
function k<T extends { a: string }>(x: Required<T>, y: { b: number }) { x = y; }
