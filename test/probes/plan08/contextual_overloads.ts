// @strict: true
interface Checker {
    getFalseType(fresh?: boolean): number;
    getFalseType(): number;
}
const c: Checker = { getFalseType: (fresh?) => fresh ? 1 : 2 };
type F = ((a: string) => void) | ((a: string) => number);
const f: F = a => { a.length; };
type G = ((a: string) => void) | ((a: number) => void);
const g: G = a => {};
