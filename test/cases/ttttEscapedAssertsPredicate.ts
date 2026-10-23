// `asserts` spelled with an escape is the keyword all the same (tsc's scanner makes it AssertsKeyword): an asserts
// predicate, with TS1260 for the escape. Babel's assert-predicate/invalid-escaped-asserts-keyword fixture
declare function assertIsString(value: unknown): \u{61}sserts value;
declare function assertIsNumber(value: unknown): \u0061sserts value is number;
