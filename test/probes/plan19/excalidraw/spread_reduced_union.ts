// @strict: true
type Base = { id: string; x: number };
type Rect = Base & { type: "rect" };
type Arrow = Base & { type: "arrow"; points: number[] };
// The first member is never (conflicting discriminants): tsc's getReducedType drops it before spreading
declare const e1: (Rect & { index: string } & { type: "arrow" }) | (Arrow & { index: string });
const s1: { id: string; index: string } = { ...e1 };
declare const e2: (Rect & { type: "arrow" }) | Arrow;
const s2: { id: string } = { ...e2, x: 1 };
