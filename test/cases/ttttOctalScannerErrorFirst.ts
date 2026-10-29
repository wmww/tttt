// A legacy octal literal's TS1121 is the scanner's, reported as the token is read, before the parser's error at it:
// `"2015-01-"02""` is TS1121 at `02`, not ',' expected (seed 20261114, useRegexpGroups num-to-str)
declare function f(...a: any[]): void;
f("2015-01-"02"");
