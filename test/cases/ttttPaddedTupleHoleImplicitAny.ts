// @strict: true
// A hole in an array pattern parameter past its tuple default's length is padded with any like a name, and tsc's
// reportImplicitAny says so of the OmittedExpression, named `(Missing)` (bun 99-V4 checkDeclarationInitializer__03)
function g([c, , d] = [""]) {}
