// @declaration: true
// @target: es2022
// tsgo's getNamedMembers puts a class's members "contained by" it first, by text range without looking at the file:
// Base's members, whose offsets in a.ts fall inside Mixed's range in b.ts, sort with Mixed's own (babel-parser's
// plugin mixins). mixin2's class is earlier in its file: its own member first
// @filename: a.ts
// Padding so that Base's members sit at offsets inside Mixed's text range in b.ts.
// ..........................................................................................
// ..........................................................................................
export class Base {
    alpha(): void {}
    beta(): void {}
}
export class Far {
    gamma(): void {}
}
// @filename: b.ts
import { Base } from "./a";
export const mixin = (sup: typeof Base) => class Mixed extends sup {
    // ..........................................................................................
    // ..........................................................................................
    // ..........................................................................................
    own(): void {}
};
export const mixin2 = (sup: typeof Base) => class extends sup {
    own2(): void {}
};
