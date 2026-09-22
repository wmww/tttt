// @strict: true
// @target: esnext
interface S { emitSignatures?: Map<string, string>; outFile?: string }
declare const composite: boolean | undefined;
function f(oldState: Readonly<S> | undefined, out: string | undefined) {
    const can = composite && oldState?.emitSignatures && !out;
    if (can) {
        oldState.emitSignatures.get("a");
    }
}
