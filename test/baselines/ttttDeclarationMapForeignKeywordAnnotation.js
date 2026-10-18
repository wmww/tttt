//// [test/cases/ttttDeclarationMapForeignKeywordAnnotation.ts] ////

//// [c.ts]
export class Q { m(code: number): void {} }
//// [d.ts]
export class R { m(code: number): void {} }
import { Q } from "./c";
export const v = new Q().m;


//// [c.js]
export class Q {
    m(code) { }
}
//// [d.js]
export class R {
    m(code) { }
}
import { Q } from "./c";
export const v = new Q().m;


//// [c.d.ts]
export declare class Q {
    m(code: number): void;
}
//# sourceMappingURL=c.d.ts.map//// [d.d.ts]
export declare class R {
    m(code: number): void;
}
export declare const v: (code: number) => void;
//# sourceMappingURL=d.d.ts.map