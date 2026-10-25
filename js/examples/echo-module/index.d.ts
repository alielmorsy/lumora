declare const Echo: {
  /** Returns `s` unchanged, from C++. */
  echo(s: string): string;
  /** Resolves with `s` after `ms` milliseconds, from a C++ thread through the invoker. */
  later(s: string, ms: number): Promise<string>;
};
export default Echo;
