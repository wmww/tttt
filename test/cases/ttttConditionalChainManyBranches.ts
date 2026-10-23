// @strict: true
// A chain of more than 32 conditional types (opencode: @solid-primitives/event-listener's event maps): the walk
// collecting the type parameters it mentions remembers every true branch it has walked, however many
interface T0 { kT0: 1 }
interface T1 { kT1: 1 }
interface T2 { kT2: 1 }
interface T3 { kT3: 1 }
interface T4 { kT4: 1 }
interface T5 { kT5: 1 }
interface T6 { kT6: 1 }
interface T7 { kT7: 1 }
interface T8 { kT8: 1 }
interface T9 { kT9: 1 }
interface T10 { kT10: 1 }
interface T11 { kT11: 1 }
interface T12 { kT12: 1 }
interface T13 { kT13: 1 }
interface T14 { kT14: 1 }
interface T15 { kT15: 1 }
interface T16 { kT16: 1 }
interface T17 { kT17: 1 }
interface T18 { kT18: 1 }
interface T19 { kT19: 1 }
interface T20 { kT20: 1 }
interface T21 { kT21: 1 }
interface T22 { kT22: 1 }
interface T23 { kT23: 1 }
interface T24 { kT24: 1 }
interface T25 { kT25: 1 }
interface T26 { kT26: 1 }
interface T27 { kT27: 1 }
interface T28 { kT28: 1 }
interface T29 { kT29: 1 }
interface T30 { kT30: 1 }
interface T31 { kT31: 1 }
interface T32 { kT32: 1 }
interface T33 { kT33: 1 }
interface T34 { kT34: 1 }
interface T35 { kT35: 1 }
interface T36 { kT36: 1 }
interface T37 { kT37: 1 }
interface T38 { kT38: 1 }
interface T39 { kT39: 1 }
type Target = T0 | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T8 | T9 | T10 | T11 | T12 | T13 | T14 | T15 | T16 | T17 | T18 | T19 | T20 | T21 | T22 | T23 | T24 | T25 | T26 | T27 | T28 | T29 | T30 | T31 | T32 | T33 | T34 | T35 | T36 | T37 | T38 | T39;
type MapOf<X> = X extends T0 ? { ev: T0 } : X extends T1 ? { ev: T1 } : X extends T2 ? { ev: T2 } : X extends T3 ? { ev: T3 } : X extends T4 ? { ev: T4 } : X extends T5 ? { ev: T5 } : X extends T6 ? { ev: T6 } : X extends T7 ? { ev: T7 } : X extends T8 ? { ev: T8 } : X extends T9 ? { ev: T9 } : X extends T10 ? { ev: T10 } : X extends T11 ? { ev: T11 } : X extends T12 ? { ev: T12 } : X extends T13 ? { ev: T13 } : X extends T14 ? { ev: T14 } : X extends T15 ? { ev: T15 } : X extends T16 ? { ev: T16 } : X extends T17 ? { ev: T17 } : X extends T18 ? { ev: T18 } : X extends T19 ? { ev: T19 } : X extends T20 ? { ev: T20 } : X extends T21 ? { ev: T21 } : X extends T22 ? { ev: T22 } : X extends T23 ? { ev: T23 } : X extends T24 ? { ev: T24 } : X extends T25 ? { ev: T25 } : X extends T26 ? { ev: T26 } : X extends T27 ? { ev: T27 } : X extends T28 ? { ev: T28 } : X extends T29 ? { ev: T29 } : X extends T30 ? { ev: T30 } : X extends T31 ? { ev: T31 } : X extends T32 ? { ev: T32 } : X extends T33 ? { ev: T33 } : X extends T34 ? { ev: T34 } : X extends T35 ? { ev: T35 } : X extends T36 ? { ev: T36 } : X extends T37 ? { ev: T37 } : X extends T38 ? { ev: T38 } : X extends T39 ? { ev: T39 } : never;
declare function mk<T extends Target, M extends MapOf<T>, K extends keyof M>(t: T, k: K, h: (e: M[K]) => void): void;
declare const t0: T0;
mk(t0, "ev", (e) => { e.kT0; });
declare function mk2<T extends Target, M extends MapOf<T>>(t: T): M;
const m = mk2(t0);
