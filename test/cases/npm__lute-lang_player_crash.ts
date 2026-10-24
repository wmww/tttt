// origin: npm @lute-lang/player@0.48.1 https://github.com/journeyWorker/lute packages/player/tsconfig.json seed 20261212
// flip: yes
// @strict: true
// @noEmit: true
// A type alias with 32 type parameters (xstate 6's Next_MachineConfig has 36): alias_instance()'s fixed 32-word
// intern key overflows, `check(count <= c_arraylen(stack_key))` aborts tttt (exit 134, no output). tsgo checks it.
type Many<T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20, T21, T22, T23, T24, T25, T26, T27, T28, T29, T30, T31> = [T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20, T21, T22, T23, T24, T25, T26, T27, T28, T29, T30, T31];
export type M = Many<number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number>;
export const m: M = null!;
