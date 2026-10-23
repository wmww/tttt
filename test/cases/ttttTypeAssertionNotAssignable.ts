// A type assertion `<T>x` is no left-hand side expression: `<string>foo = '100'` ends before the `=` (tsc's
// parseAssignmentExpressionOrHigher, isLeftHandSideExpression), TS1005 ';' expected. Babel's
// cast/unparenthesized-type-assertion-and-assign fixture
declare let foo: string;
<string>foo = '100';
(<string>foo) = '100';
