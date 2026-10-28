// @strict: true
// @noEmit: true
// A for-of's declared variable is typed before its expression is checked (tsc's checkForOfStatement checks the
// declaration list first): with an evolving array the loop's back edge pushes the variable, which is circular then
// (bun 99-V4 checkForOfStatement__05: TS7022 on entry, any)
export {}
declare function more(): boolean;
const queue = [];
queue.push(1);
while (more()) {
    for (const entry of queue) {
        queue.push(entry);
        const probe: never = entry;
    }
}
