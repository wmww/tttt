class C { x: string; m(): void {}; get g() { return 1 } }
interface C { x: number; m: number; g: string; y: number }
declare const c: C;
c.x; c.m; c.g;
interface D { m(): void; p: number; q(): void }
class D { m: string; p() {} ; q() {} }
declare const d: D;
d.m; d.p; d.q;
class E { x = 1; x: string; }
