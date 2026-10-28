// An interface whose base is an intersection that contains it, directly or through another interface: TS2310 (tsc's
// hasBaseType looks through intersections)
interface L0 extends L1 { q0: 1 }
interface L1 extends M { }
type M = L0 & { z: 1 };
interface K0 extends N { }
type N = K0 & { z: 1 };
interface J0 extends J1 { }
interface J1 extends J0 { }
declare const k: K0
k.z
