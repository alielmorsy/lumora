import type { JSX as SolidJSX } from "solid-js";

/** A mounted element as handed to `ref`. */
export interface LumoraElement {
  readonly tag: string;
}

export type CSSProperties = { [name: string]: string | number | null | undefined };

export interface LumoraEvent {
  [key: string]: unknown;
}

type Handler = (e: LumoraEvent) => void;

export namespace JSX {
  // Same as Solid's so <Show>, <For> and other solid-js components type-check against Lumora elements.
  type Element = SolidJSX.Element;

  interface ElementChildrenAttribute {
    children: {};
  }

  interface CommonProps {
    class?: string;
    style?: string | CSSProperties;
    ref?: LumoraElement | ((el: LumoraElement) => void);
    children?: Element;
    onClick?: Handler;
    onDoubleClick?: Handler;
    onPress?: Handler;
    onRelease?: Handler;
    onPointerMove?: Handler;
    onPointerEnter?: Handler;
    onPointerLeave?: Handler;
    onScroll?: Handler;
    onFocus?: Handler;
    onBlur?: Handler;
    onKeyDown?: Handler;
    onKeyUp?: Handler;
  }

  interface ViewProps extends CommonProps {}
  interface TextProps extends CommonProps {}
  interface ButtonProps extends CommonProps {
    disabled?: boolean;
  }
  interface InputProps extends CommonProps {
    value?: string;
    placeholder?: string;
    disabled?: boolean;
    /** Read when the node is first inserted. */
    multiline?: boolean;
    onInput?: (e: { value: string }) => void;
    onSubmit?: (e: { value: string }) => void;
  }
  interface ImageProps extends CommonProps {
    src?: string;
    fit?: "fill" | "contain" | "cover" | "none";
  }
  interface ScrollProps extends CommonProps {
    /** Read when the node is first inserted. */
    axis?: "vertical" | "horizontal" | "both";
  }

  interface IntrinsicElements {
    view: ViewProps;
    text: TextProps;
    button: ButtonProps;
    input: InputProps;
    image: ImageProps;
    scroll: ScrollProps;
  }
}
