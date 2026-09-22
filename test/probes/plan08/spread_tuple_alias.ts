// @strict: true
type Args = (string | number)[];
type DiagAndArgs = [message: string, ...args: Args];
declare function reportError(message: string, ...args: Args): void;
let stackVar: DiagAndArgs[] | undefined;
function f() {
    const stack = stackVar || [];
    stackVar = undefined;
    if (stack.length === 1) {
        reportError(...stack[0]);
    }
}
