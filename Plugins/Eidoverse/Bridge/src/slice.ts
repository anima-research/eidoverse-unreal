// src/slice.ts — what a slice is and what it is given (the table itself is src/slices.ts).

/** What a slice is given: the renderer, the bridge's own requests, the fold. */
export interface Ctx {
  /** To the renderer. */
  send(m: Record<string, unknown>): void;
  /** A local refusal to the renderer: bridge/error {for, code, reason}. */
  error(forType: string, code: string, reason: string): void;
  /** A request the bridge itself makes ({id: "bridge:<n>"}); its reply comes back here, never to the renderer. */
  request(verb: string, args?: unknown, opts?: { timeoutMs?: number }): Promise<any>;
  /** The engine's replica (shared/protocol/replica.js), null before the first cut. Read only: its records are frozen. */
  readonly replica: any;
  /** The replica's view (shared/protocol/view.js viewState), null before the first cut. */
  view(): any;
  /** The server this connection dialled (bridge/hello), null before it. */
  readonly server: string | null;
  /** The renderer's join as it went up (replica + clock added), null before it. */
  readonly join: Record<string, unknown> | null;
  /** This slice's state for this connection, made on first use. */
  own<T>(init: () => T): T;
}

export interface Slice {
  name: string;
  /** bridge/* messages from the renderer this slice answers. */
  local?: Record<string, (ctx: Ctx, msg: any) => void | Promise<void>>;
  /** Taps on server messages (not snapshot or log: see adopted / ingested). Return true to consume it (not forwarded). */
  server?: Record<string, (ctx: Ctx, msg: any, raw: string) => boolean | void>;
  /** After a cut is adopted (the join, a re-join, a resync); `snap` is the snapshot without its cut, `cut` the cut as sent (its presence: holds). */
  adopted?(ctx: Ctx, snap: any, cut: any): void;
  /** After a log message is folded: `r` is replica.ingest's result {changed, says, ts, actor, …}. */
  ingested?(ctx: Ctx, r: any, msg: any): void;
  closed?(ctx: Ctx): void;
}

/** A stub's answer to a message its slice hasn't built yet. */
export const unbuilt = (slice: string) => (ctx: Ctx, msg: any) => ctx.error(msg.type, "unbuilt", `${msg.type} arrives with slice ${slice}`);
