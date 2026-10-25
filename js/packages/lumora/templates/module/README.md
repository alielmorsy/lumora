# {{npmName}}

A Lumora native module. `hello()` returns a string from C++ on desktop and from Kotlin on Android.

```ts
import {{Name}} from "{{npmName}}";

console.log({{Name}}.hello());
```

## Layout

- `index.js`, `index.d.ts`: the JS entry and its types.
- `native/cpp`: the C++ module (JSI), built as the static CMake target `lumora_{{lower}}`.
- `native/android`: the Kotlin module (`{{androidPackage}}.{{Name}}Module`), an Android library depending on `:lumora-android`.

The `"lumora"` field in `package.json` tells `lumora dev` where to find each implementation.
