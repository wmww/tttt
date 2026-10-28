declare class D { private readonly id: string | number; other: 1 }
type Writable<T> = { -readonly [K in keyof T]: T[K] }
type W = Pick<Writable<D>, 'id'>
declare const wd: W & D
const t: { id: string | number } = wd
wd.id;
declare class E { protected id: string; private p: number; q: 1 }
declare const we: Pick<Writable<E>, 'id' | 'p'> & E
we.id; we.p;
const t2: { id: string, p: number } = we
class F extends E { m(x: Pick<Writable<E>, 'id'> & E) { x.id; const y: { id: string } = x } }
declare const wdp: D & { id: string | number }
wdp.id = 'x'
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x'; t.other; }
function g2<T extends D & { id: string | number }>(t: T) { t.id; }
