// @strict: true
export function b1() { return; return 1; return 1; }
export function b2() { return; return "s"; }
export function b5() { return; return true; }
export function b6() { return; return {a: 1}; }
export function b7() { return; return null; }
export function b8() { return; return 10n; }
export async function b9() { return; return 1; }
export const b10 = () => { return; return 1; };
export const b11 = function () { return; return `x`; };
class C { m() { return; return 1; } get g() { return; return 1; } }
export function f6() { return 1; return; }
export function b7() { return; return null; }
class C { get g() { return; return 1; } get h() { return 1; return; } get k() { return 1; } }
export function n1(x: boolean) { if (x) return; return 1; }
declare const b: boolean;
class C { get g() { if (b) return; return 1; } get h() { if (b) return 2; return 1; } }
const o = { get g() { if (b) return; return 1; } };
