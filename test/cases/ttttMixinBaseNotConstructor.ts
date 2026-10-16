// A class extending a type variable whose constraint isn't a constructor type (TS2507) has no base type, so it isn't
// a mixin and its constructor isn't checked (TS2545) (mutation of doubleMixinConditionalTypeBaseClassWorks)
const M = <C extends never>(Base: C) => class extends Base {};
function f<C extends never>(Base: C) { return class extends Base {}; }
function g<C extends string>(Base: C) { return class extends Base {}; }
function h<C extends new (x: number) => {}>(Base: C) { return class extends Base {}; }
