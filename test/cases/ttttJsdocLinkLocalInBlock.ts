// @strict: true
// @noUnusedLocals: true
// A JSDoc `{@link x}` before a statement in a block names x from there: a local only a link names is used (bun's 31-SC5)
export function k(): number {
    if (Math.random() > 0.5) {
        const limit = 10;
        /** At most {@link limit}. */
        const x = 1;
        return x;
    }
    for (const item of [1]) {
        /** See {@linkcode item}. */
        return 1;
    }
    const lim = 10;
    /* Not a JSDoc: {@link lim} */
    const y = 1;
    return y;
}
