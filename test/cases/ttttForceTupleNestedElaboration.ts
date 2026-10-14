// @strict: false
// @noEmit: true
// The elaboration of an array literal argument re-reads it as a tuple (tsc's elaborateArrayLiteral with forceTuple),
// and forceTuple reaches nested array literals and a spread conditional's branches: the spread element is
// `[string, Toggle]` (no `| undefined` without strictNullChecks), which fits HotkeyItem, so nothing is elaborated
// and the error stays on the argument (docmost's `getHotkeyHandler([..., ...(editable ? [["alt+R", t]] : [])])`)
type HotkeyItem = [string, (event: any) => void, {}?];
declare function getHotkeyHandler(hotkeys: HotkeyItem[]): void;
type SSA<S> = S | ((prevState: S) => S);
declare function useToggle<T = boolean>(options?: readonly T[]): [T, (value?: SSA<T>) => void];
const [caseSensitive, caseSensitiveToggle] = useToggle([
    { isCaseSensitive: false, color: "gray" },
    { isCaseSensitive: true, color: "blue" },
]);
const [r, replaceToggle] = useToggle([
    { isR: false, color: "gray" },
]);
declare const editable: boolean;
const next = () => {};
const previous = () => {};
getHotkeyHandler([
  ["Enter", next],
  ["shift+Enter", previous],
  ["alt+C", caseSensitiveToggle],
  ...(editable ? [["alt+R", replaceToggle]] : []),
]);
