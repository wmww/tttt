import { Base } from "no-such-module";
export class C extends Base {
    own = 1;
}
const c = new C();
c.on("x");
C.staticThing();
declare const anyBase: any;
class D extends anyBase {}
new D().whatever;
