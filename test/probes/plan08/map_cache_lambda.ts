// @strict: true
// @target: esnext
enum K { A, B, C, D }
type Op = K.A | K.B | K.C;
interface E { e: 1 }
interface Rules { get(kind: Op): (node: E) => E }
function outer(): Rules {
    let cache: Map<Op, (node: E) => E> | undefined;
    return { get };
    function get(kind: Op) {
        cache ||= new Map();
        let rule = cache.get(kind);
        if (!rule) {
            rule = node => wrap(kind, node);
            cache.set(kind, rule);
        }
        return rule;
    }
    function wrap(kind: Op, node: E): E { return node; }
}
