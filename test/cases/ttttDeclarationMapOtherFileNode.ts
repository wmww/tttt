// @declaration: true
// @declarationMap: true
// An annotation of another file the node builder reuses has no positions in the file being written: not mapped
// @filename: a.ts
export declare function g(): {
    /** a long comment to move the positions past what b.ts has */
    message: string;
    kind: "a" | "b";
};
// @filename: b.ts
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
// padding so that the positions of a.ts's nodes are inside this file too
import { g } from "./a";
export const r = g();
