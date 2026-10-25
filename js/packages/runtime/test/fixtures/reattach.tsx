import { createSignal, render, Show } from "@lumora/runtime";

const [on, setOn] = createSignal(true);
globalThis.setOn = setOn;
const box = (
  <view class="box">
    <text>X</text>
    <input multiline />
  </view>
);
render(() => (
  <view>
    <text>a</text>
    <Show when={on()}>{box}</Show>
  </view>
));
