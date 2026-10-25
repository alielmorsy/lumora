# hello

A Lumora app written in TSX with Solid.

## Run

```
npm run dev
```

`lumora dev` bundles `src/main.tsx` and opens it in the Lumora host. Saving a file rebuilds and reloads the window.
It needs a built `LumoraJsHost`: set the `LUMORA_HOST` environment variable to its path, or pass
`npm run dev -- --host <path>`.

## Files

- `src/main.tsx` mounts the app.
- `src/App.tsx` is a counter.
- `src/app.css` styles it. Import CSS files from any module.
