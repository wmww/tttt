// origin: npm @shepai/cli@1.247.0 https://github.com/shep-ai/shep tsconfig.bench.json seed 20261302
// flip: yes
// @strict: true
// @target: es2022
// @filename: notifier.ts
interface NotificationLike {
  on?(event: 'failed', listener: (event: unknown, error: string) => void): unknown;
}
export interface Deps {
  createNotification: (opts: { title: string; body: string }) => NotificationLike;
}
// @filename: test.ts
import type { Deps } from './notifier';
declare function fn<T extends (...args: any[]) => any>(impl?: T): Mock<T>;
interface Mock<T extends (...args: any[]) => any> { (...args: Parameters<T>): ReturnType<T>; mock: T }
declare let deps: Deps;
deps.createNotification = fn(() => ({
  show: fn(),
  on: fn((event: string, listener: (...args: unknown[]) => void) => {}),
}));
