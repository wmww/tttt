// @strict: true
// A union property's optionality takes in a union member that lacks it but has an applicable index signature, an
// intersection member's too (tsc's createUnionOrIntersectionProperty): Pick over react-transition-group's
// TransitionGroupProps keeps the intrinsic element's attributes optional (mui-x x-date-pickers' styled
// TransitionGroup, TS2740/TS2322 there)
type U = ({ a?: string; b?: number } & { c?: boolean }) | ({ component: any } & { [prop: string]: any });
type P = Pick<U, "a" | "b">;
const x: P = {};
const y: Pick<U, "b"> = { b: 1 };
