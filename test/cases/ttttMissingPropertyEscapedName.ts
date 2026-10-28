// A missing property named with escapes is printed as written (tsc's declarationNameToString), TS2551 and TS2339;
// an element access's key by its value
class C { arg3 = 1; }
declare const c: C;
c.argX\u0033;
c.b\u0061r = 1;
c["b\u0061z"];
