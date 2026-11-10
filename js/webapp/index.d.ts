// Types of @eskiu/webapp.

export interface Event<T> {
  event: string;
  payload: T;
  /** The window that sent it (the program's window id); 0 from the program to every window. */
  windowId: number;
}
export type UnlistenFn = () => void;

export interface DialogFilter { name: string; extensions: string[] }
export interface OpenDialogOptions {
  title?: string;
  defaultPath?: string;
  directory?: boolean;
  multiple?: boolean;
  filters?: DialogFilter[];
}
export interface SaveDialogOptions { title?: string; defaultPath?: string; filters?: DialogFilter[] }
export interface MessageDialogOptions {
  title?: string;
  kind?: "info" | "warning" | "error";
  okLabel?: string;
  cancelLabel?: string;
}

export interface CloseRequestedEvent { preventDefault(): void }
export interface EskiuWindow {
  readonly id: number;
  setTitle(title: string): Promise<void>;
  setSize(width: number, height: number): Promise<void>;
  setMinSize(width: number, height: number): Promise<void>;
  setFullscreen(on: boolean): Promise<void>;
  isFullscreen(): Promise<boolean>;
  focus(): Promise<void>;
  openDevtools(): Promise<void>;
  /** Asks to close, as the close button does (onCloseRequested handlers run). */
  close(): Promise<void>;
  /** Closes without asking. */
  destroy(): Promise<void>;
  onCloseRequested(handler: (event: CloseRequestedEvent) => void | Promise<void>): Promise<UnlistenFn>;
}

export function isEskiu(): boolean;
export function platform(): "macos" | "windows" | "linux" | "web";
export function invoke<T = unknown>(cmd: string, args?: Record<string, unknown>): Promise<T>;
export function listen<T = unknown>(event: string, handler: (event: Event<T>) => void): Promise<UnlistenFn>;
export function once<T = unknown>(event: string, handler: (event: Event<T>) => void): Promise<UnlistenFn>;
export function emit(event: string, payload?: unknown): Promise<void>;
export function convertFileSrc(path: string, protocol?: string): string;
export function schemeUrl(scheme: string, path?: string): string;
export function open(options?: OpenDialogOptions & { multiple?: false }): Promise<string | null>;
export function open(options: OpenDialogOptions & { multiple: true }): Promise<string[] | null>;
export function save(options?: SaveDialogOptions): Promise<string | null>;
export function message(text: string, options?: string | MessageDialogOptions): Promise<void>;
export function ask(text: string, options?: string | MessageDialogOptions): Promise<boolean>;
export function confirm(text: string, options?: string | MessageDialogOptions): Promise<boolean>;
export function getCurrentWindow(): EskiuWindow;
export function quit(code?: number): Promise<void>;
