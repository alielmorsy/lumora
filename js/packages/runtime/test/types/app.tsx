import { createSignal, For, render, Show, type LumoraElement } from "@lumora/runtime";

function Row(props: { label: string }) {
  return <text class="row">{props.label}</text>;
}

function App() {
  const [value, setValue] = createSignal("");
  let box!: LumoraElement;
  return (
    <view class="app" style={{ padding: 8, flexGrow: 1 }} ref={box} onClick={() => setValue("")}>
      <text style="color: red" onPointerEnter={(e) => console.log(e)}>
        Hello {value()}
      </text>
      <button disabled={value() === ""} onPress={() => {}} onKeyDown={() => {}}>
        <text>Go</text>
      </button>
      <input
        value={value()}
        placeholder="type"
        multiline
        onInput={(e) => setValue(e.value)}
        onSubmit={(e) => setValue(e.value.trim())}
      />
      <image src="logo.png" fit="contain" ref={(el) => console.log(el.tag)} />
      <scroll axis="vertical" onScroll={() => {}}>
        <Show when={value()} fallback={<text>empty</text>}>
          <Row label={value()} />
        </Show>
        <For each={["a", "b"]}>{(s) => <Row label={s} />}</For>
      </scroll>
      <view
        // @ts-expect-error unknown event prop
        onClik={() => {}}
      />
    </view>
  );
}

render(() => <App />);
