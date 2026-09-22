// @strict: true
declare function returnFalse(): false;
declare function returnUndefined(): undefined;
interface Opts {
    onA?: (x: number, y: string) => boolean;
    onB?: (x: number) => void;
}
function f({ onA = returnFalse, onB = returnUndefined }: Opts) {
    onA(1, "");
    onB(1);
}
