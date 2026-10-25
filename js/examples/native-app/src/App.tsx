import { createSignal } from "@lumora/runtime";
import Echo from "@lumora-examples/echo-module";

export default function App() {
  const [later, setLater] = createSignal("");
  Echo.later("hi", 0).then(setLater);
  return (
    <view style="padding: 16px">
      <text>echo: {Echo.echo("hi")}</text>
      <text>later: {later()}</text>
    </view>
  );
}
