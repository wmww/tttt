// @strict: true
// @lib: es2022
// An object literal getter typed first by a call's resolution sees `this` as any (ty_contextual_this_type(): the
// checker's lazy_getter, per-worker in-flight state). A local of its body evaluated then is that history's: with the
// shared graph it was published clean, so a checker that met state.ts after another had resolved `s` from user.ts
// took `end` from the graph, evaluated the initializer only at the body's own check (this = S) and reported TS2531
// that one checker (and tsgo) don't (vidstack's player-state.ts at K16). The read is a taint event now. The suite's
// round robin deal checks state.ts first, so only a deal like vidstack's (user.ts on an earlier checker) shows it
// @filename: state.ts
declare class State<T> { constructor(record: T); }
export interface S { live: number | null; ready: boolean; end: number }
export const s = new State<S>({
  live: null,
  ready: false,
  get end() {
    const end = this.live > 0 ? this.live : this.ready ? 1 : 0;
    return end;
  },
});
// @filename: user.ts
import { s } from './state';
export class P { static state = s; }
