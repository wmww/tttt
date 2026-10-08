// The first pass of a rest parameter's inference checks a literal argument under the call's inference (ConstructorParameters<C>[0]
// sees C's candidate W), so `get` has a contextual signature at its first check: no TS2322 (tsc: none)
interface Snap { a: number; state: 'x' | 'y' }
declare function create<C extends new (...args: any[]) => unknown>(ctor: C, ...args: ConstructorParameters<C>): void;
class W { constructor(o: { get: () => Snap, send: (r: { q: string }) => boolean }) {} }
declare const b: boolean; declare const s: Snap;
create(W, { get: () => b ? { ...s, state: "y" } : s, send: ({ q }) => true });
