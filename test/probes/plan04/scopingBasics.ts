// Only errors tsc reports: N.hidden (TS2339) and the let/var clash (TS2451 x2)
var a = 1;
var a = 2;
function f() { for (var i = 0; i < 1; i++) {} return i; }
function g(x: number) { var x = 1; return x; }
if (a) { var hoisted = 1; }
hoisted;
interface I { a: number }
var I: number;
namespace N { export var p = 1; var hidden = 2; export interface T { a: number } }
namespace N { export var q = p; export let t: T; }
let z = N.q + N.hidden;
function fundule() { return fundule.extra; }
namespace fundule { export const extra = 1; }
class K { m() { return K.t; } }
namespace K { export const t = 1; }
enum E { A }
enum E { B = 1 }
let e = E.A | E.B;
let clash = 1;
var clash = 2;
