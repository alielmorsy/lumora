import { createSignal, render } from "@lumora/runtime";

const [count, setCount] = createSignal(0);
render(() => (
  <view>
    <text>Count: {count()}</text>
    <button onClick={() => setCount(count() + 1)} />
  </view>
));
