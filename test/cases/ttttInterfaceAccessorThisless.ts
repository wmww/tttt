// @strict: true
// @lib: es2015
// An accessor doesn't propagate a `this` type to its interface (tsc's binder: no ContainerFlagsPropagatesThisKeyword):
// an interface whose only `this` is in accessor signatures is thisless, and that `this` is the error type
interface I { get x(): this }
declare const i: I;
const r: null = i.x;
interface K { get x(): this; w: number }
declare const k: K;
const r2: null = k.x;
interface L { w: number; get x(): this }
declare const l: L;
const r3: null = l.x;
interface J0 { get y(): this; m(): void }
declare const j0: J0;
const b0: null = j0.y;
interface M { m(): void; get y(): this }
declare const m: M;
const b2: null = m.y;
interface N { get y(): this; z: this }
declare const n: N;
const b3: null = n.y;
interface S { set x(v: this); get y(): number }
declare const s: S;
s.x = s;
interface B { get x(): this }
interface B { z: this }
declare const b: B;
const a2: null = b.x;
interface D extends B { get w(): this }
declare const d: D;
const a3: null = d.w;
interface E { get [Symbol.iterator](): this }
type TL = { get x(): this };
interface F { get x(): { y: this } }
declare const f: F;
const a4: null = f.x.y;
interface H { get x(): () => this }
declare const h: H;
const a5: null = h.x();
interface E { get [Symbol.iterator](): this }
declare const e: E;
const a6: null = e[Symbol.iterator];
interface E2 { get ["k"](): this }
declare const e2: E2;
const a7: null = e2.k;
