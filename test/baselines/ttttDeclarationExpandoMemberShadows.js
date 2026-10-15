//// [test/cases/ttttDeclarationExpandoMemberShadows.ts] ////

//// [visitors.ts]
function verify$1(x: number): void {}
export { verify$1 as verify };
//// [a.ts]
import * as visitors from "./visitors";
export { visitors };
export default function traverse(n: number) {}
traverse.visitors = visitors;
traverse.verify = visitors.verify;
//// [b.ts]
import * as visitors from "./visitors";
import { verify } from "./visitors";
export default function walk(n: number) {}
walk.verify = visitors.verify;
walk.v2 = verify;


//// [visitors.js]
function verify$1(x) { }
export { verify$1 as verify };
//// [a.js]
import * as visitors from "./visitors";
export { visitors };
export default function traverse(n) { }
traverse.visitors = visitors;
traverse.verify = visitors.verify;
//// [b.js]
import * as visitors from "./visitors";
import { verify } from "./visitors";
export default function walk(n) { }
walk.verify = visitors.verify;
walk.v2 = verify;


//// [visitors.d.ts]
declare function verify$1(x: number): void;
export { verify$1 as verify };
//// [a.d.ts]
import * as visitors from "./visitors";
export { visitors };
declare function traverse(n: number): void;
export default traverse;
declare namespace traverse {
    export { visitors };
    export var verify: typeof import("./visitors").verify;
}
//// [b.d.ts]
import * as visitors from "./visitors";
import { verify } from "./visitors";
declare function walk(n: number): void;
export default walk;
declare namespace walk {
    var _a: typeof visitors.verify;
    export { _a as verify };
    export { verify as v2 };
}
