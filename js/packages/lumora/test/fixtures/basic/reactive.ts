import { createRoot, createSignal, createEffect } from "solid-js";

const runs: number[] = [];
const setN = createRoot(() => {
  const [n, setN] = createSignal(1);
  createEffect(() => {
    runs.push(n());
  });
  return setN;
});
setN(2);
(globalThis as any).runs = runs;
