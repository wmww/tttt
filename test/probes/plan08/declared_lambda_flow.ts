// @strict: true
interface E { e: 1 }
function outer(): { get(): ((n: E) => E) | undefined } {
    return { get };
    function get() {
        let rule: ((n: E) => E) | undefined = node => node;
        return rule;
    }
}
