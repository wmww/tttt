// A reserved word where a parameter name goes is TS1359 and a missing identifier, which takes ThisNodeHasError
// (createIdentifierWithDiagnostic): the parameter's TS6133 is hidden
// @noUnusedLocals: true
// @noUnusedParameters: true
var o = {
    method(null) {
    },
};
function f(null) {}
class C { m(null) {} }
