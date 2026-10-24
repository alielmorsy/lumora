import { createSignal, For, render } from "@lumora/runtime";

const [items, setItems] = createSignal(["a", "b", "c"]);
globalThis.setItems = setItems;
render(() => (
  <view>
    <For each={items()}>{(s) => <text>{s}</text>}</For>
  </view>
));
