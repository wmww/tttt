// origin: npm @bumblebee032/ng-feedback@2.0.97 https://github.com/DarrenLouw/feedback tsconfig.bench.json seed 20261109
// flip: no
// @emitDecoratorMetadata: true
// @experimentalDecorators: true
// @lib: es2017, dom
// @module: es6
// @moduleResolution: bundler
// (paths is an object: not expressible as a header) {}
// @target: es6
// @typeRoots: ./node_modules/@types
// @useDefineForClassFields: false
// @filename: node_modules/@angular/core/package.json
{"name": "@angular/core", "version": "6.1.10", "main": "./bundles/core.umd.js", "module": "./fesm5/core.js", "typings": "./core.d.ts"}
// @filename: node_modules/rxjs/operators/package.json
{"name": "rxjs/operators", "typings": "./index.d.ts", "main": "./index.js", "module": "../_esm5/operators/index.js"}
// @filename: node_modules/rxjs/package.json
{"name": "rxjs", "version": "6.3.3", "main": "./index.js", "typings": "./index.d.ts", "module": "./_esm5/index.js"}
// @filename: package.json
{"name": "feedback", "version": "0.0.0"}
// @filename: node_modules/@angular/core/core.d.ts
export * from './index';
// @filename: node_modules/@angular/core/index.d.ts
export * from './public_api';
// @filename: node_modules/@angular/core/public_api.d.ts
export * from './src/core';
// @filename: node_modules/@angular/core/src/change_detection.d.ts
export { ChangeDetectionStrategy, ChangeDetectorRef, CollectionChangeRecord, DefaultIterableDiffer, IterableChangeRecord, IterableChanges, IterableDiffer, IterableDifferFactory, IterableDiffers, KeyValueChangeRecord, KeyValueChanges, KeyValueDiffer, KeyValueDifferFactory, KeyValueDiffers, NgIterable, PipeTransform, SimpleChange, SimpleChanges, TrackByFunction, WrappedValue } from './change_detection/change_detection';
// @filename: node_modules/@angular/core/src/change_detection/change_detection.d.ts
export { SimpleChanges } from '../metadata/lifecycle_hooks';
export { SimpleChange, WrappedValue, devModeEqual } from './change_detection_util';
export { ChangeDetectorRef } from './change_detector_ref';
export { ChangeDetectionStrategy, ChangeDetectorStatus, isDefaultChangeDetectionStrategy } from './constants';
export { DefaultIterableDiffer } from './differs/default_iterable_differ';
export { CollectionChangeRecord, IterableChangeRecord, IterableChanges, IterableDiffer, IterableDifferFactory, IterableDiffers, NgIterable, TrackByFunction } from './differs/iterable_differs';
export { KeyValueChangeRecord, KeyValueChanges, KeyValueDiffer, KeyValueDifferFactory, KeyValueDiffers } from './differs/keyvalue_differs';
export { PipeTransform } from './pipe_transform';
// @filename: node_modules/@angular/core/src/change_detection/change_detection_util.d.ts
export declare function devModeEqual(a: any, b: any): boolean;
/**
 */
export declare class WrappedValue {
}
/**
 */
export declare class SimpleChange {
}
// @filename: node_modules/@angular/core/src/change_detection/change_detector_ref.d.ts
/**
 */
export declare abstract class ChangeDetectorRef {
    /**
     */
}
// @filename: node_modules/@angular/core/src/change_detection/constants.d.ts
export declare enum ChangeDetectionStrategy {
    /**
     */
}
export declare enum ChangeDetectorStatus {
    /**
     */
}
export declare function isDefaultChangeDetectionStrategy(changeDetectionStrategy: ChangeDetectionStrategy): boolean;
// @filename: node_modules/@angular/core/src/change_detection/differs/default_iterable_differ.d.ts
import { IterableChangeRecord, IterableChanges, IterableDiffer, IterableDifferFactory, NgIterable, TrackByFunction } from './iterable_differs';
/**
 */
export declare class DefaultIterableDiffer<V> implements IterableDiffer<V>, IterableChanges<V> {
}
// @filename: node_modules/@angular/core/src/change_detection/differs/iterable_differs.d.ts
export declare type NgIterable<T> = Array<T> | Iterable<T>;
/**
 */
export interface IterableDiffer<V> {
}
/**
 */
export interface IterableChanges<V> {
}
/**
 */
export interface IterableChangeRecord<V> {
}
/**
 */
export interface CollectionChangeRecord<V> extends IterableChangeRecord<V> {
}
/**
 */
export interface TrackByFunction<T> {
}
/**
 */
export interface IterableDifferFactory {
}
/**
 */
export declare class IterableDiffers {
}
// @filename: node_modules/@angular/core/src/change_detection/differs/keyvalue_differs.d.ts
/**
 */
export interface KeyValueDiffer<K, V> {
    /**
     */
}
/**
 */
export interface KeyValueChanges<K, V> {
    /**
     */
}
/**
 */
export interface KeyValueChangeRecord<K, V> {
}
/**
 */
export interface KeyValueDifferFactory {
    /**
     */
}
/**
 */
export declare class KeyValueDiffers {
    /**
     */
}
// @filename: node_modules/@angular/core/src/change_detection/pipe_transform.d.ts
export interface PipeTransform {
}
// @filename: node_modules/@angular/core/src/core.d.ts
/**
 */
export * from './metadata';
export * from './render';
export * from './linker';
export * from './change_detection';
// @filename: node_modules/@angular/core/src/di/injection_token.d.ts
export declare class InjectionToken<T> {
}
// @filename: node_modules/@angular/core/src/linker.d.ts
export { ElementRef } from './linker/element_ref';
// @filename: node_modules/@angular/core/src/linker/element_ref.d.ts
/**
 */
export declare class ElementRef<T = any> {
    /**
     */
}
// @filename: node_modules/@angular/core/src/metadata.d.ts
export { ANALYZE_FOR_ENTRY_COMPONENTS, Attribute, ContentChild, ContentChildDecorator, ContentChildren, ContentChildrenDecorator, Query, ViewChild, ViewChildDecorator, ViewChildren, ViewChildrenDecorator } from './metadata/di';
export { Component, ComponentDecorator, Directive, DirectiveDecorator, HostBinding, HostListener, Input, Output, Pipe } from './metadata/directives';
export { AfterContentChecked, AfterContentInit, AfterViewChecked, AfterViewInit, DoCheck, OnChanges, OnDestroy, OnInit } from './metadata/lifecycle_hooks';
// @filename: node_modules/@angular/core/src/metadata/di.d.ts
/**
 */
import { InjectionToken } from '../di/injection_token';
/**
 */
export declare const ANALYZE_FOR_ENTRY_COMPONENTS: InjectionToken<any>;
/**
 */
export interface Attribute {
}
/**
 */
export declare abstract class Query {
}
/**
 */
export interface ContentChildrenDecorator {
    /**
     */
}
/**
 */
export declare const ContentChildren: ContentChildrenDecorator;
/**
 */
export interface ContentChildDecorator {
    /**
     */
}
/**
 */
export declare const ContentChild: ContentChildDecorator;
/**
 */
export interface ViewChildrenDecorator {
    /**
     */
}
/**
 */
export declare type ViewChildren = Query;
/**
 */
export interface ViewChildDecorator {
    /**
     */
}
/**
 */
export declare const ViewChild: ViewChildDecorator;
// @filename: node_modules/@angular/core/src/metadata/directives.d.ts
/**
 */
export interface DirectiveDecorator {
    /**
     */
}
/**
 */
export declare const Directive: DirectiveDecorator;
export interface ComponentDecorator {
    /**
     */
}
/**
 */
export declare const Component: ComponentDecorator;
/**
 */
export interface Pipe {
    /**
     */
}
/**
 */
export interface InputDecorator {
    /**
     */
}
/**
 */
export declare const Input: InputDecorator;
/**
 */
export interface OutputDecorator {
    /**
    */
}
/**
 */
export declare const Output: OutputDecorator;
/**
 */
export interface HostBindingDecorator {
    /**
     */
}
export declare const HostBinding: HostBindingDecorator;
/**
 */
export interface HostListenerDecorator {
}
/**
 */
export declare const HostListener: HostListenerDecorator;
// @filename: node_modules/@angular/core/src/metadata/lifecycle_hooks.d.ts
/**
 */
export interface SimpleChanges {
}
/**
 */
export interface OnChanges {
}
/**
 */
export interface OnInit {
    /**
     */
}
/**
 */
export interface DoCheck {
    /**
       */
}
/**
 */
export interface OnDestroy {
    /**
     */
}
/**
 */
export interface AfterContentInit {
    /**
     */
}
/**
 */
export interface AfterContentChecked {
    /**
     */
}
/**
 */
export interface AfterViewInit {
    /**
     */
}
/**
 */
export interface AfterViewChecked {
    /**
     */
}
// @filename: node_modules/@angular/core/src/render.d.ts
export { RenderComponentType, Renderer, Renderer2, RendererFactory2, RendererStyleFlags2, RendererType2, RootRenderer } from './render/api';
// @filename: node_modules/@angular/core/src/render/api.d.ts
export declare class RenderComponentType {
}
export declare abstract class Renderer {
}
export declare abstract class RootRenderer {
}
export interface RendererType2 {
    /**
     */
}
export declare abstract class RendererFactory2 {
}
export declare enum RendererStyleFlags2 {
}
export declare abstract class Renderer2 {
    /**
     */
}
// @filename: node_modules/rxjs/index.d.ts
export { Observable } from './internal/Observable';
export { Subscription } from './internal/Subscription';
export { from } from './internal/observable/from';
export { fromEvent } from './internal/observable/fromEvent';
// @filename: node_modules/rxjs/internal/Observable.d.ts
import { Subscription } from './Subscription';
import { TeardownLogic, OperatorFunction, PartialObserver, Subscribable } from './types';
/**
 */
export declare class Observable<T> implements Subscribable<T> {
    /**
     */
    subscribe(next?: (value: T) => void, error?: (error: any) => void, complete?: () => void): Subscription;
    /**
     */
    pipe<A>(op1: OperatorFunction<T, A>): Observable<A>;
    pipe<A, B>(op1: OperatorFunction<T, A>, op2: OperatorFunction<A, B>): Observable<B>;
}
// @filename: node_modules/rxjs/internal/Subscription.d.ts
import { SubscriptionLike, TeardownLogic } from './types';
/**
 */
export declare class Subscription implements SubscriptionLike {
}
// @filename: node_modules/rxjs/internal/observable/from.d.ts
import { Observable } from '../Observable';
import { ObservableInput, SchedulerLike } from '../types';
export declare function from<T>(input: ObservableInput<ObservableInput<T>>, scheduler?: SchedulerLike): Observable<Observable<T>>;
// @filename: node_modules/rxjs/internal/observable/fromEvent.d.ts
import { Observable } from '../Observable';
export interface NodeStyleEventEmitter {
}
export interface NodeCompatibleEventEmitter {
}
export interface JQueryStyleEventEmitter {
}
export interface HasEventTargetAddRemove<E> {
}
export declare type EventTargetLike<T> = HasEventTargetAddRemove<T> | NodeStyleEventEmitter | NodeCompatibleEventEmitter | JQueryStyleEventEmitter;
export declare type FromEventTarget<T> = EventTargetLike<T> | ArrayLike<EventTargetLike<T>>;
export declare function fromEvent<T>(target: FromEventTarget<T>, eventName: string): Observable<T>;
// @filename: node_modules/rxjs/internal/operators/filter.d.ts
import { OperatorFunction, MonoTypeOperatorFunction } from '../types';
export declare function filter<T, S extends T>(predicate: (value: T, index: number) => value is S, thisArg?: any): OperatorFunction<T, S>;
// @filename: node_modules/rxjs/internal/operators/finalize.d.ts
import { MonoTypeOperatorFunction } from '../types';
/**
 */
export declare function finalize<T>(callback: () => void): MonoTypeOperatorFunction<T>;
// @filename: node_modules/rxjs/internal/operators/first.d.ts
import { OperatorFunction } from '../../internal/types';
export declare function first<T, D = T>(predicate?: null, defaultValue?: D): OperatorFunction<T, T | D>;
// @filename: node_modules/rxjs/internal/operators/map.d.ts
import { OperatorFunction } from '../types';
/**
 */
export declare function map<T, R>(project: (value: T, index: number) => R, thisArg?: any): OperatorFunction<T, R>;
// @filename: node_modules/rxjs/internal/operators/mergeMap.d.ts
import { ObservableInput, OperatorFunction } from '../types';
export declare function mergeMap<T, R>(project: (value: T, index: number) => ObservableInput<R>, concurrent?: number): OperatorFunction<T, R>;
// @filename: node_modules/rxjs/internal/operators/scan.d.ts
import { OperatorFunction, MonoTypeOperatorFunction } from '../types';
export declare function scan<T>(accumulator: (acc: T, value: T, index: number) => T, seed?: T): MonoTypeOperatorFunction<T>;
// @filename: node_modules/rxjs/internal/operators/skipWhile.d.ts
import { MonoTypeOperatorFunction } from '../types';
/**
 */
export declare function skipWhile<T>(predicate: (value: T, index: number) => boolean): MonoTypeOperatorFunction<T>;
// @filename: node_modules/rxjs/internal/operators/takeUntil.d.ts
import { Observable } from '../Observable';
import { MonoTypeOperatorFunction } from '../types';
export declare function takeUntil<T>(notifier: Observable<any>): MonoTypeOperatorFunction<T>;
// @filename: node_modules/rxjs/internal/operators/timeout.d.ts
import { MonoTypeOperatorFunction, SchedulerLike } from '../types';
/**
 */
export declare function timeout<T>(due: number | Date, scheduler?: SchedulerLike): MonoTypeOperatorFunction<T>;
// @filename: node_modules/rxjs/internal/types.d.ts
import { Observable } from './Observable';
export interface UnaryFunction<T, R> {
    (source: T): R;
}
export interface OperatorFunction<T, R> extends UnaryFunction<Observable<T>, Observable<R>> {
}
export interface MonoTypeOperatorFunction<T> extends OperatorFunction<T, T> {
}
export interface Unsubscribable {
}
export declare type TeardownLogic = Unsubscribable | Function | void;
export interface SubscriptionLike extends Unsubscribable {
}
export declare type SubscribableOrPromise<T> = Subscribable<T> | Subscribable<never> | PromiseLike<T> | InteropObservable<T>;
export interface Subscribable<T> {
    subscribe(next?: (value: T) => void, error?: (error: any) => void, complete?: () => void): Unsubscribable;
}
export declare type ObservableInput<T> = SubscribableOrPromise<T> | ArrayLike<T> | Iterable<T>;
export declare type InteropObservable<T> = {
};
export interface NextObserver<T> {
    complete?: () => void;
}
export interface ErrorObserver<T> {
    complete?: () => void;
}
export interface CompletionObserver<T> {
    complete: () => void;
}
export declare type PartialObserver<T> = NextObserver<T> | ErrorObserver<T> | CompletionObserver<T>;
export interface Observer<T> {
}
export interface SchedulerLike {
}
// @filename: node_modules/rxjs/operators/index.d.ts
export { filter } from '../internal/operators/filter';
export { finalize } from '../internal/operators/finalize';
export { first } from '../internal/operators/first';
export { map } from '../internal/operators/map';
export { mergeMap } from '../internal/operators/mergeMap';
export { scan } from '../internal/operators/scan';
export { skipWhile } from '../internal/operators/skipWhile';
export { takeUntil } from '../internal/operators/takeUntil';
export { timeout } from '../internal/operators/timeout';
