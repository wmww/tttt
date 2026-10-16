//// [test/cases/ttttConstContextConditionalBranches.ts] ////

//// [ttttConstContextConditionalBranches.ts]
// A conditional expression's branches aren't in a const context for the conditional being in one (tsc's
// isConstContext passes through parentheses, array literals, spreads and property assignments only): in `as const`,
// `c ? "a" : ["b"]` is `"a" | string[]`; a const type parameter's contextual type still makes them const (pi's
// keybindings printed `"ctrl+up" | ["ctrl+shift+up", "ctrl+up"]`)
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


//// [ttttConstContextConditionalBranches.js]
export const K = {
    a: { defaultKeys: w ? "ctrl+up" : ["ctrl+shift+up", "ctrl+up"] },
    b: { defaultKeys: ["x", "y"] },
};
export const K2 = {
    a: { defaultKeys: w ? "ctrl+up" : ["ctrl+shift+up", "ctrl+up"] },
};
export const K3 = f(w ? ["a"] : ["b", 1]);
export const K5 = [w ? "a" : "b", w ? ["x"] : 1];


//// [ttttConstContextConditionalBranches.d.ts]
export declare const K: {
    readonly a: {
        readonly defaultKeys: "ctrl+up" | string[];
    };
    readonly b: {
        readonly defaultKeys: ["x", "y"];
    };
};
export declare const K2: {
    readonly a: {
        readonly defaultKeys: "ctrl+up" | string[];
    };
};
export declare const K3: readonly ["a"] | readonly ["b", 1];
export declare const K5: readonly ["a" | "b", 1 | string[]];
