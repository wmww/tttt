// @noEmit: true
// A namespace body missing its `}` is tsc's parseModuleBlock: a plain TS1005, no related TS1007 pointing at its `{`
// (mutation testing seed 20261104, contextualReturnTypeOfIIFE3)
declare namespace app {
  var foo: {
    bar: number;
  };
