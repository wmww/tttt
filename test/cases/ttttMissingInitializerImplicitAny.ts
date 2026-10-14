// @strict: true
// `const x = ;` has tsc's missing identifier as its initializer (an error type): no TS7005, unlike `declare const y;`
const syntaxError = ;
let b = ;
declare const y;
export {}
