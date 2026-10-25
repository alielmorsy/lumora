import type { JSX } from "./jsx-runtime";

export type { JSX, LumoraElement, LumoraEvent, CSSProperties } from "./jsx-runtime";
export {
  createSignal,
  createEffect,
  createMemo,
  createResource,
  createContext,
  useContext,
  batch,
  untrack,
  on,
  onMount,
  onCleanup,
  mergeProps,
  Show,
  For,
  Index,
  Switch,
  Match,
  ErrorBoundary,
  Suspense,
} from "solid-js";

/** Applies the imported CSS, mounts `code()` into the host root and returns a function that unmounts it. */
export function render(code: () => JSX.Element): () => void;
