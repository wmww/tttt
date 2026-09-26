// @strict: true
type Nest<S extends string> = S extends `${infer F}${infer R}` ? F extends "x" ? [] : F extends "y" ? [] : F extends "z" ? [] : F extends "w" ? [] : [0, ...Nest<R>] : [];
declare const b: Nest<"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz">;
declare const c: Nest<"abcdefghijklmnopqrstuv">;
