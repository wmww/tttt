// A call a loop's unfinished flow analysis evaluates reports on the partial types (tsc's getTypeOfExpression is a
// full check and the resolution isn't cached in a loop): TS2345 'string' besides the walk's 'string | number'
// @strict: true
// @target: es2017
declare let cond: boolean;
async function asNumber(x: number): Promise<number> { return x; }
function syncNumber(x: number): number { return x; }
async function h2() {
    let x: string | number | boolean;
    x = "0";
    while (cond) {
        x = await asNumber(x) + 1;
        x;
    }
}
function s2() {
    let x: string | number | boolean;
    x = "0";
    while (cond) {
        x = syncNumber(x) + 1;
        x;
    }
}
