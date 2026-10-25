import { requireNativeModule } from "@lumora/runtime";

globalThis.echo = requireNativeModule("Echo");
try {
  requireNativeModule("Nope");
} catch (e) {
  globalThis.err = e.message;
}
