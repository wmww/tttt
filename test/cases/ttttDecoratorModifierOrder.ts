declare const a: any, b: any;
@a export export @b class C1 {}
@a public export @b class C2 {}
@a declare export @b class C3 {}
function f() { @a export @b class C4 {} }
namespace N { export @a default class C5 {} }
namespace M { @a export default @b class C6 {} }

namespace N2 { export @a default class C5 {} }
export @a default class C6b {}
namespace M { export @a class C7 {} }
declare namespace Q2 { export @a default class C8 {} }
declare module "x" { export @a default class C9 {} }
