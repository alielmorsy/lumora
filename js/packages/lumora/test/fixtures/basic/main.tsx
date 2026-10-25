import { createSignal } from "solid-js";
import "./app.css";
import { label } from "./util";

export function App() {
  const [n] = createSignal(1);
  return (
    <view class="card">
      <text>{label(n())}</text>
    </view>
  );
}

App();
