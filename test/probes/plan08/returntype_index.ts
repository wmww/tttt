// @strict: true
function createAddOutput() {
    let outputs: string[] | undefined;
    return { addOutput, getOutputs };
    function addOutput(path: string | undefined) { }
    function getOutputs(): readonly string[] { return outputs || []; }
}
type R = ReturnType<typeof createAddOutput>;
type A = R["addOutput"];
function f(addOutput: ReturnType<typeof createAddOutput>["addOutput"]) {
    addOutput("x");
}
