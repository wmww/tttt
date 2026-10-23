// @noUnusedLocals: true
// @noEmit: true
// @filename: x.ts
// A private member read only by a later file is unused when x.ts is checked: tsc reports a file's unused members when
// it has checked it (checkSourceFile), so only the reads of the files checked before count. In tttt the marks carry the
// earliest file that read the member, over every checker (program_member_referenced()), so the report doesn't depend on
// the checker count or on which checker read it
export class C { private x = 1; private y = 2 }
// @filename: y.ts
import { C } from "./x";
new C()["x"];
// @filename: z.ts
import "./w";
export class D { private z = 1 }
// @filename: w.ts
import { D } from "./z";
new D()["z"];
