// A type parameter list's element that isn't one is TS1139 (tsc's parseBracketedList(TypeParameters), its
// parsingContextErrors), a `,` too: babel's anonymous-function-generator-invalid fixtures
const fn = function* <,>(input: number) { yield input; };
class C<T,,U> {}
function f<,T>() {}
