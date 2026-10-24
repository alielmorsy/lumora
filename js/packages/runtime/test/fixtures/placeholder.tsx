import { createSignal, render, Show } from "@lumora/runtime";

const [on] = createSignal(false);
const [s, setS] = createSignal("");
globalThis.setS = setS;
render(() => (
  <view>
    <view class="shows">
      <text>a</text>
      <Show when={on()}>
        <view />
      </Show>
    </view>
    <view class="bare">
      <text>a</text>
      {s()}
      <view />
    </view>
  </view>
));
