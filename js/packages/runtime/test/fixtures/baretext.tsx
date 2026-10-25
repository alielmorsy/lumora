import { createSignal, render } from "@lumora/runtime";

const [n, setN] = createSignal(0);
globalThis.setN = setN;
render(() => <view>n={n()}</view>);
