// @strict: true
// semver.ts: a shorthand property holding a parameter's (regular) literal union doesn't widen
interface Comparator {
    readonly operator: "<" | "<=" | "=";
    readonly operand: number;
}
function createComparator(operator: Comparator["operator"], operand: number) {
    return { operator, operand };
}
const comparators: Comparator[] = [];
comparators.push(createComparator("<", 1));
