// @strict: true
// A class merged with an interface: each declaration's overloads are a group of the merged symbol, and overload
// resolution tries a later declaration's group first (tsc's reorderCandidates), the class's own too (matrix-js-sdk's
// module augmentation `interface SecretsBundle { to_json(): Promise<J> }` before the class's `to_json(): unknown`)
declare class X { m(): unknown }
interface X { m(): Promise<number> }
declare const x: X;
const r1 = x.m();
interface Y { m(): unknown }
interface Y { m(): Promise<number> }
declare const y: Y;
const r2 = y.m();
declare class Z { m(): unknown; m(): Promise<number> }
declare const z: Z;
const r3 = z.m();
interface W { m(): Promise<number> }
declare class W { m(): unknown }
declare const w: W;
const r4 = w.m();
