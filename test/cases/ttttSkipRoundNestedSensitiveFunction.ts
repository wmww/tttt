// @strict: true
// A round that leaves out the context-sensitive arguments (tsc's CheckMode.SkipContextSensitive) also leaves out a
// context-sensitive function inside an argument that isn't one itself (`o && ((x) => ...)`): an overload that fails
// on another argument doesn't type it for good. Without a context-sensitive argument there is no such round, and the
// first overload types it (effect's Channel.catchIf: the refinement overload, then the predicate one)
declare const c: {
    <E, EB extends E>(e: E, refinement: (e: E) => e is EB, f: (x: EB) => void, orElse?: ((x: Exclude<E, EB>) => void) | undefined): void;
    <E>(e: E, predicate: (e: E) => boolean, f: (x: E) => void, orElse?: ((x: E) => void) | undefined): void;
};
declare const c2: {
    <E, EB extends E>(e: E, refinement: (e: E) => e is EB, orElse?: ((x: Exclude<E, EB>) => void) | undefined): void;
    <E>(e: E, predicate: (e: E) => boolean, orElse?: ((x: E) => void) | undefined): void;
};
function g<E>(e: E, p: (e: E) => boolean, o?: (e: E) => void) {
    c(e, p, (x) => {}, o && ((x) => o(x)));
    c(e, p, (x) => {}, (0, (x) => o!(x)));
}
function h(e: string | number, p: (e: string | number) => boolean, o?: (e: string | number) => void) {
    c2(e, p, o && ((x) => o(x)));
}
