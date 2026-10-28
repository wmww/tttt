// A getter and setter pair of an object literal agree on `abstract` (TS2676) and the getter is at least as accessible as
// the setter (TS2808), at both names (tsc's checkAccessorDeclaration): their modifiers are TS1042 there, but the
// declarations still have them
const o3 = { abstract get x() { return 1 }, set x(v) {} };
const o4 = { get y() { return 1 }, abstract set y(v) {} };
const o5 = { private get z() { return 1 }, set z(v) {} };
const o6 = { protected get w() { return 1 }, private set w(v) {} };
const o7 = { set u(v) {}, abstract get u() { return 1 } };
