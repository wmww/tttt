// @noTypesAndSymbols: true
// @filename: a.ts
// A syntax error inside a function body the parser defers (plan 78), in a program whose other file has a syntax
// error at its top level: both are reported (the bodies were only parsed after a resolve walk that a gated program
// never runs)
export function f() {
    let x = ;
    return x;
}

// @filename: b.ts
export const y = ;
