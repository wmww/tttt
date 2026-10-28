// A name right after a shorthand property's `=` is that property's child: not found, it is TS18004 like the property's own
// name (tsc's getCannotFindNameDiagnosticForName; mutation testing, noUnusedLocals_writeOnly)
declare let x: number | undefined;
({ x = used } = { x: 1 });
({ x = (used2) } = { x: 1 });
for ({ x = used4 } of [{ x: 1 }]);
function f(y = 0, b = false) {
    ({ y = used5 } = { y: b ? 1 : undefined });
}
