// @noEmit: true
// An `import x = N` outside a namespace or module (TS1232) is never resolved: tsc's checkImportEqualsDeclaration stops
// at checkGrammarModuleElementContext, so no TS2503 for a value or a missing name (mutation seed 20261105,
// unknownSymbols2)
function f(asdf: any) {
    import d = asdf;
}
var asdf2 = 1;
{
    import e = asdf2;
}
function g() {
    import h = nothere;
    import i = nothere.deeper;
}
