// @strict: true
type AbstractConstructor<T, A extends unknown[] = any[]> = abstract new (...args: A) => T;
abstract class Bar {
  abstract barMethod(): void;
}
function withBar<T extends AbstractConstructor<object>>(Ctor: T) {
  abstract class ExtendedBar extends Ctor {
    extendedBarMethod() {}
  }
  return ExtendedBar;
}
const W = withBar(Bar);
class Wrong extends withBar(Bar) {}
declare const w: InstanceType<typeof W>;
w.barMethod;
w.extendedBarMethod;

type Census = { count: number };
abstract class AbstractBuilding<T = unknown> {
  owners: T;
  constructor(o: T) { this.owners = o; }
}
class CityBlock<BT extends AbstractBuilding<Census>> {
  residence: BT;
  constructor(H: abstract new (...a: [Census]) => BT) {
    class Building extends H {}
    this.residence = new Building({count: 2});
  }
}
