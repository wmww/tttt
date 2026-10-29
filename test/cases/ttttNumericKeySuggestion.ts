// A number literal key names a property by its text: TS2551 suggests a near name (tsc's getPropertyNameFromIndex),
// and TS7053 has no 'Property ... does not exist' chain for it
// @strict: true
var obj2 = { a: 1, 5.462437423415177e+244: true, abcdef: 2 };
obj2[5.462437423415177];
obj2["5.462437423415177"];
obj2[5.46243742341517];
obj2["abcdeg"];
obj2[12345];
