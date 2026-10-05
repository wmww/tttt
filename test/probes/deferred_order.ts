// Deferred checks (tsc's checkNodeDeferred): a class expression's members and a void operand are checked after the
// file's statements, so their literal types are made after q's and q, q2, q3 print "xa" | "xb"
const C = class { p = Math.random() ? "lb" : "la"; m() { return Math.random() ? "mb" : "ma"; } };
const q = Math.random() ? "la" : "lb";
const q2 = Math.random() ? "ma" : "mb";
const v = void (Math.random() ? "vb" : "va");
const q3 = Math.random() ? "va" : "vb";
