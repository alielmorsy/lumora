import { createSignal, render } from "@lumora/runtime";

const t = <text>T</text>;
const x = <text>X</text>;
const [a, setA] = createSignal([t, x]);
const [b, setB] = createSignal<any[]>([]);
globalThis.api = { setA, setB, x, t };
render(() => (
  <view>
    <view>{a()}</view>
    <view>{b()}</view>
  </view>
));
