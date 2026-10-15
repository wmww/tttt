//// [test/cases/ttttDeclarationExportedPattern.ts] ////

//// [ttttDeclarationExportedPattern.ts]
// A non-exported destructuring whose names an export list exports is written whole
declare function d(): { get: () => number; set: (v: number) => void };
/** doc */
const { get: g1, set: s1 } = d();
const [a1, b1] = [1, 2];
export { g1, s1, a1 };
export const { get: g2 } = d();


//// [ttttDeclarationExportedPattern.js]
/** doc */
const { get: g1, set: s1 } = d();
const [a1, b1] = [1, 2];
export { g1, s1, a1 };
export const { get: g2 } = d();


//// [ttttDeclarationExportedPattern.d.ts]
/** doc */
declare const { get: g1, set: s1 }: {
    get: () => number;
    set: (v: number) => void;
};
declare const [a1, b1]: [number, number];
export { g1, s1, a1 };
export declare const { get: g2 }: {
    get: () => number;
    set: (v: number) => void;
};
