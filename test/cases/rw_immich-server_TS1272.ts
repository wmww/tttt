// origin: immich-server (named) https://github.com/immich-app/immich server/tsconfig.bench.json seed 20269002 (commit d474ec96)
// flip: no
// Cut by hand from server/src/services/job.service.ts: a decorated method whose rest parameter is a binding pattern
// typed by a type alias imported without `import type`; tsc checks only a rest type's first type argument
// @experimentalDecorators: true
// @emitDecoratorMetadata: true
// @isolatedModules: true
// @module: nodenext
// @target: es2023
// @strict: true
// @filename: package.json
{ "name": "immich", "type": "module" }
// @filename: src/repositories/event.repository.ts
type EventMap = { JobRun: [string, { name: string }]; JobStart: [string] };
export type ArgsOf<T extends keyof EventMap> = EventMap[T];
// @filename: src/decorators.ts
export const OnEvent = (options: { name: string }): MethodDecorator => () => {};
// @filename: src/services/job.service.ts
import { OnEvent } from '../decorators.js';
import { ArgsOf } from '../repositories/event.repository.js';
export class JobService {
  @OnEvent({ name: 'JobRun' })
  async onJobRun(...[queueName, job]: ArgsOf<'JobRun'>) {
    return [queueName, job.name];
  }
}
