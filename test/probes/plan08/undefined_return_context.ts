// @strict: true
let restore = () => undefined;
function f() {
    const current = restore;
    restore = () => {
        restore = current;
    };
}
