import { createSignal } from "@lumora/runtime";
import "./app.css";

export default function App() {
  const [count, setCount] = createSignal(0);
  return (
    <view class="app">
      <text class="count">Count: {count()}</text>
      <button class="increment" onClick={() => setCount(count() + 1)}>
        <text>Increment</text>
      </button>
    </view>
  );
}
