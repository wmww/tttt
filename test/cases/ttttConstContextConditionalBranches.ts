// A conditional expression's branches aren't in a const context for the conditional being in one (tsc's
// isConstContext passes through parentheses, array literals, spreads and property assignments only): in `as const`,
// `c ? "a" : ["b"]` is `"a" | string[]`; a const type parameter's contextual type still makes them const (pi's
// keybindings printed `"ctrl+up" | ["ctrl+shift+up", "ctrl+up"]`)
// @strict: true
// @declaration: true
declare const w: boolean;
type Def = { defaultKeys: string | string[] };
export const K = {
  a: { defaultKeys: w ? "ctrl+up" : ["ctrl+shift+up", "ctrl+up"] },
  b: { defaultKeys: ["x", "y"] },
} as const satisfies Record<string, Def>;
export const K2 = {
  a: { defaultKeys: w ? "ctrl+up" : ["ctrl+shift+up", "ctrl+up"] },
} as const;
declare function f<const T>(t: T): T;
export const K3 = f(w ? ["a"] : ["b", 1]);
export const K5 = [w ? "a" : "b", w ? ["x"] : 1] as const;
