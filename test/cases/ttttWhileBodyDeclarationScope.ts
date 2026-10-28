// A `let`/`const` as the whole body of a while or do statement (TS1156) is declared where the loop is: those aren't
// block scope containers to tsc's binder (a for statement is), so the name is found after the loop
// (missing_checks.md)
declare let a: boolean;
while (a) const x = 1;
x;
do const y = 1; while (a);
y;
for (;;) const z = 2;
z;
