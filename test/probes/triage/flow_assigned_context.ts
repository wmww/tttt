// @strict: true
// What an assignment narrows to is its value checked with the target's type as context, whether or not the walk
// reached the assignment yet: the return type asked for at the first `return` checks `pair[1]` before
// `pair = [1, ""]` was walked. The value without context is (string | number)[], which narrows nothing (TS18048)
declare const flag: boolean;
export function g() {
    let pair: [number, string] | undefined;
    if (flag) {
        return 1;
    }
    pair = [1, ""];
    const name = pair[1];
    return name;
}
