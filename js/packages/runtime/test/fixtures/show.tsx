import { createSignal, render, Show } from "@lumora/runtime";

const [on, setOn] = createSignal(false);
globalThis.setOn = setOn;
render(() => (
  <view>
    <Show when={on()}>
      <text>X</text>
    </Show>
  </view>
));
