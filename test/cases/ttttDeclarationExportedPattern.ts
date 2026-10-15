// @declaration: true
// A non-exported destructuring whose names an export list exports is written whole
declare function d(): { get: () => number; set: (v: number) => void };
/** doc */
const { get: g1, set: s1 } = d();
const [a1, b1] = [1, 2];
export { g1, s1, a1 };
export const { get: g2 } = d();
