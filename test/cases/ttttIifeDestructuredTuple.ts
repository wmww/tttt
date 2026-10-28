// @strict: true
// An immediately invoked function initializing an array pattern returns into the pattern's implied tuple type (tsc's
// getContextualReturnType of an IIFE is the call's contextual type): its array literals are tuples, so each name gets
// its own element (coop's networkingService fetch wrapper: `const [finalBody, extraHeaders] = (() => ...)()`)
declare const body: string | number;
declare function f(): boolean;
const [finalBody, extraHeaders] = (() => {
  if (f()) {
    return [body, {}];
  }
  return [true, { a: 1 }];
})();
const x: string | number | boolean = finalBody;
const [p, q] = (() => [body, {}])();
const y: string | number = p;
const [r, s] = (function () { return [1, 'a']; })();
const z: number = r;
