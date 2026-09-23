// tsc's getQuickTypeOfExpression: the type of `f(x)` with a single non-generic signature doesn't need x
interface Sym { name: string }
interface Typ { flags: number }
declare function getTypeOfSymbol(s: Sym): Typ;
declare function getPropertyOfType(t: Typ, name: string): Sym | undefined;
declare function more(): boolean;
function walk(start: Sym) {
    let symbol: Sym | undefined = start;
    while (more()) {
        const type = getTypeOfSymbol(symbol);
        symbol = getPropertyOfType(type, "x");
        if (!symbol) return undefined;
    }
    return symbol;
}
interface Result { value: string | number | undefined }
declare function compute(member: string, auto: number | undefined): Result;
function values(members: string[]) {
    let autoValue: number | undefined = 0;
    for (const member of members) {
        const result = compute(member, autoValue);
        autoValue = typeof result.value === "number" ? result.value + 1 : undefined;
    }
}
