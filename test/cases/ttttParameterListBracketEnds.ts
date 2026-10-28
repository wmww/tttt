// @noEmit: true
// A `]` ends a parameter list (tsc's isListTerminator for Parameters), whose `)` is then missing: TS1005, not TS1138
// (mutation seed 20261105, controlFlowElementAccess2: a type literal missing its `}` reads `if (typeof c['k'] ...` as
// a method signature whose array binding pattern aborts at the string)
declare const config: {
    [key: string]: boolean;

if (typeof config['works'] !== 1) {
}
function f(]) {}
function g(a, ]) {}
type T = (a]) => 1;
