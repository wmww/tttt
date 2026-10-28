// @noEmit: true
// An object literal method or a function expression missing its body reports every element of a parameter pattern
// implicitly any, a renaming too: tsc's assignNonContextualParameterTypes asks for the parameter's type (only
// checkVariableLikeDeclaration skips a bodiless signature's renamings). Mutation seed 20261106,
// indirectDiscriminantAndExcessProperty missing a `});`
let o = {
    m({ a, b })
};
let o2 = {
    m({ a: x, b: y })
};
let o3 = {
    m({ a: x, b: "s" })
};
let f = function ({ a: x })
let g = 1;
