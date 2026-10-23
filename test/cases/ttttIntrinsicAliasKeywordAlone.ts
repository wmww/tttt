// A type alias whose body starts with `intrinsic` not before a `.` has the keyword alone as its type (tsc's
// parseTypeAliasDeclaration): what follows is TS1005 ';' expected, the keyword TS2795 outside lib. Babel's
// types/intrinsic-keyword-error fixture
type Foo = intrinsic["foo"];
type Bar = intrinsic | string;
declare namespace intrinsic { type x = number }
type Baz = intrinsic.x;
