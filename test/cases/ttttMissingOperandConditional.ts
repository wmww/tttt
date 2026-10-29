// A missing operand followed by `?` goes on as a conditional (parseConditionalExpressionRest): `case?:` is
// `<missing> ? <missing> : result += 2`, then ':' expected
declare let value: number;
let result = 0;
switch (value) {
  case?:
    result += 2;
  default:
    break;
}
