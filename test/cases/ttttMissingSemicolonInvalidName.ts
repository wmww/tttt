// @noEmit: true
// tsc's parseErrorForMissingSemicolonAfter: after a class property's name and a throw statement's expression too, its
// TS2427/TS2457/TS2819 deduplicated with the next statement's or member's error at the same token (a parse diagnostic
// of tsc's parser, not a checker's), and naming the scanner's TokenValue(), which punctuation leaves at the name's
// (bun 96-SOLID1)
class C2 { interface? ) }
class C3 { foo? ) }
class C4 { asynx foo }
class C5 { type x }
class C6 { is x }
class C7 { module { } }
class C8 { declare }
class C9 { static foo bar }
type ) ;
interface ] ;
function f() { throw type { A }; }
interface "x" ;
module 1;
namespace `t`;
type \u0061;
