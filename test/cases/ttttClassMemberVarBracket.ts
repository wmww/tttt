// A `var` or `let` followed by `[` is a class member (tsc's isClassMemberStart takes a keyword before `[`): a property
// named var, TS1440, then an index signature; `var x` isn't one (TS1068). Seed 20261112, iterableArrayPattern2 delete-stmt
class C {
    m() {}
    var [k: string]: number;
    let [n: number]: string;
}
class D {
    var x = 1;
}
