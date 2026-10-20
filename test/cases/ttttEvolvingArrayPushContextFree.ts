// A value pushed onto an evolving array is typed with the contextual type any (tsc's getContextFreeTypeOfExpression),
// so a generic call returning its type parameter infers any from it: babel's
// `children.push(this.parseLiteral(value, "JSXText"))` makes `children` any[]
// @strict: true
interface A { type: "A"; a: number }
interface B { type: "B"; b: string }
declare function parse<T extends A | B>(value: any, type: T["type"]): T;
declare function wrap<T>(value: T): { value: T };
declare function make<T = string>(): T;

const children = [];
children.push(parse(1, "B"));
const onlyB: B[] = children;
const direct = parse(1, "B");

const wrapped = [];
wrapped.push(wrap(1));
const w = wrapped;

const made = [];
made.push(make());
const m = made;

const fns = [];
fns.push((x: number) => x, 1);
const f = fns;

const literals = [];
literals.push("a", { k: 1 });
const l = literals;
