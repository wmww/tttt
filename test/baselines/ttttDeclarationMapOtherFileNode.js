//// [test/cases/ttttDeclarationMapOtherFileNode.ts] ////

//// [a.ts]
export declare function g(): {
    /** a long comment to move the positions past what b.ts has */
    message: string;
    kind: "a" | "b";
};
//// [b.ts]
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
import { g } from "./a";
export const r = g();


//// [a.js]
export {};
//// [b.js]
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
import { g } from "./a";
export const r = g();


//// [a.d.ts]
export declare function g(): {
    /** a long comment to move the positions past what b.ts has */
    message: string;
    kind: "a" | "b";
};
//# sourceMappingURL=a.d.ts.map//// [b.d.ts]
export declare const r: {
    message: string;
    kind: "a" | "b";
};
//# sourceMappingURL=b.d.ts.map