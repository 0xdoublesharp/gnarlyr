#!/usr/bin/env python3
"""Local desktop USB companion. Tk is imported only by launch()."""
import os
import queue
import sys
import threading

import echo_debug


def failure(error):
    if isinstance(error, echo_debug.DebugError):
        return {"code": error.code, "message": error.message}
    return {"code": "io_error" if isinstance(error, OSError) else "unexpected_error",
            "message": str(error) or type(error).__name__}


class Controller:
    """Main-thread state with one worker; poll() delivers definite completions.

    Mutations require the revision rendered by the caller. Requests are never queued
    behind another action, retried, or cancelled. A close waits for the worker to end.
    """
    def __init__(self, backend):
        self.backend = backend
        self.busy = False
        self.closing = False
        self.closed = False
        self.revision = 0
        self.snapshot = None
        self.error = None
        self.last_result = None
        self.last_action = None
        self._events = queue.Queue()
        self._worker = None

    def _snapshot(self):
        snapshot = {"connected": False, "status": None, "report": None,
                    "settings": None, "languages": None, "errors": {}}
        try:
            snapshot["status"] = self.backend.status()
            snapshot["connected"] = bool(snapshot["status"].get("connected"))
        except Exception as error:
            snapshot["errors"]["status"] = failure(error)
        if snapshot["connected"]:
            for key, read in (("report", self.backend.report), ("settings", self.backend.settings_list),
                              ("languages", self.backend.language_list)):
                try:
                    result = read()
                    if not result.get("ok", False):
                        snapshot["errors"][key] = result["error"]
                    else:
                        snapshot[key] = result["report"] if key == "report" else result
                except Exception as error:
                    snapshot["errors"][key] = failure(error)
        return snapshot

    def _arguments(self, action, arguments):
        snapshot = self.snapshot or {}
        if not snapshot.get("connected"):
            raise ValueError("Refresh the connected player first.")
        if action == "setting" and set(arguments) == {"setting", "value"}:
            setting, value = arguments["setting"], arguments["value"]
            entries = (snapshot.get("settings") or {}).get("settings", [])
            if (isinstance(setting, str) and isinstance(value, str) and
                    any(entry["key"] == setting and value in [option["value"] for option in entry["options"]]
                        for entry in entries)):
                return arguments
        elif action == "language" and set(arguments) == {"code"}:
            code = arguments["code"]
            entries = (snapshot.get("languages") or {}).get("languages", [])
            if isinstance(code, str) and code in [entry["code"] for entry in entries]:
                return arguments
        elif action == "install" and set(arguments) <= {"path", "select"} and "path" in arguments:
            path, select = arguments["path"], arguments.get("select", False)
            if isinstance(path, (str, os.PathLike)) and type(select) is bool:
                path = os.fspath(path)
                if isinstance(path, str) and path.strip() and "\0" not in path:
                    return {"path": path, "select": select}
        raise ValueError("Choose a supported setting, installed language or local pack file.")

    def submit(self, action, revision=None, **arguments):
        if self.busy or self.closing:
            return False
        try:
            if action == "refresh":
                if arguments:
                    raise ValueError("Refresh takes no arguments.")
            else:
                if type(revision) is not int or revision != self.revision:
                    raise ValueError("The displayed player state changed. Choose again after refreshing.")
                arguments = self._arguments(action, arguments)
        except (TypeError, ValueError) as error:
            self.error = {"code": "request_refused", "message": str(error)}
            return False
        self.revision += 1
        self.busy = True
        self.error = None
        self.last_result = None
        self.last_action = action
        self._worker = threading.Thread(target=self._run, args=(action, arguments, self.revision),
                                        name="usb-companion", daemon=False)
        try:
            self._worker.start()
        except RuntimeError as error:
            self.busy = False
            self.error = failure(error)
            return False
        return True

    def _run(self, action, arguments, revision):
        result, error = None, None
        try:
            if action == "setting":
                result = self.backend.settings_set(arguments["setting"], arguments["value"])
            elif action == "language":
                result = self.backend.language_select(arguments["code"])
            elif action == "install":
                result = self.backend.language_install(arguments["path"], select=arguments["select"])
            if result is not None and not result.get("ok", False):
                error = result["error"]
        except Exception as caught:
            error = failure(caught)
        # Refresh is read-only, including after a failed command. Never replay it.
        try:
            snapshot = self._snapshot()
        except Exception as caught:
            snapshot = {"connected": False, "settings": None, "languages": None,
                        "report": None, "status": None, "errors": {"status": failure(caught)}}
        self._events.put((revision, result, error, snapshot))

    def poll(self):
        try:
            revision, result, error, snapshot = self._events.get_nowait()
        except queue.Empty:
            return False
        self._worker.join()
        if revision == self.revision:
            self.snapshot = snapshot
            self.last_result = result
            self.error = error or snapshot["errors"].get("status")
            self.busy = False
            self.closed = self.closing
        return True

    def request_close(self):
        self.closing = True
        self.closed = not self.busy
        return self.closed


BACKGROUND = "#11151e"
PANEL = "#1c2432"
TEXT = "#edf4fa"
MUTED = "#adbccd"
CYAN = "#5cdee8"
PINK = "#ff79b0"


class Window:
    """Tk widgets stay on the caller's main thread; backend injection is for QA."""
    def __init__(self, root, tk, ttk, filedialog, backend):
        self.root, self.tk, self.ttk, self.filedialog = root, tk, ttk, filedialog
        self.controller = Controller(backend)
        self.mutators = []
        self.readonly = []
        self.setting_rows = []
        self.languages = []
        self.view_revision = 0
        self.path = tk.StringVar()
        self.select_after_install = tk.BooleanVar(value=True)
        self.language_choice = tk.StringVar()
        self.message = tk.StringVar(value="Reading USB player…")
        self.summary = tk.StringVar(value="Connect the player in USB storage mode.")
        self.details = tk.StringVar(value="")
        self.availability = tk.StringVar(value="")
        self._style()
        root.title(echo_debug.IDENTITY["name"] + " USB Companion")
        root.geometry("760x740")
        root.minsize(600, 580)
        root.configure(background=BACKGROUND)
        root.protocol("WM_DELETE_WINDOW", self.close)
        if sys.platform == "darwin":
            root.createcommand("::tk::mac::Quit", self.close)
        root.bind("<Command-r>", lambda event: self.start("refresh"))
        root.bind("<Command-q>", lambda event: self.close())
        root.columnconfigure(0, weight=1)
        root.rowconfigure(1, weight=1)
        heading = ttk.Frame(root, padding=(20, 16, 20, 12))
        heading.grid(row=0, column=0, sticky="ew")
        heading.columnconfigure(0, weight=1)
        ttk.Label(heading, text=echo_debug.IDENTITY["name"] + " · USB Companion", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        self.refresh_button = ttk.Button(heading, text="Refresh", command=lambda: self.start("refresh"))
        self.refresh_button.grid(row=0, column=1, padx=(16, 0))
        ttk.Label(heading, textvariable=self.summary, style="Accent.TLabel").grid(row=1, column=0, columnspan=2, sticky="w", pady=(10, 0))
        self.details_label = ttk.Label(heading, textvariable=self.details, wraplength=680)
        self.details_label.grid(row=2, column=0, columnspan=2, sticky="w", pady=(4, 0))
        # Scroll the complete form so every control remains reachable in a small window.
        scroller = ttk.Frame(root)
        scroller.grid(row=1, column=0, sticky="nsew", padx=20)
        scroller.rowconfigure(0, weight=1)
        scroller.columnconfigure(0, weight=1)
        self.canvas = tk.Canvas(scroller, background=BACKGROUND, highlightthickness=0, borderwidth=0)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        bar = ttk.Scrollbar(scroller, orient="vertical", command=self.canvas.yview)
        bar.grid(row=0, column=1, sticky="ns")
        self.canvas.configure(yscrollcommand=bar.set)
        body = ttk.Frame(self.canvas)
        self.form = self.canvas.create_window((0, 0), window=body, anchor="nw")
        self.canvas.bind("<Configure>", lambda event: self.canvas.itemconfigure(self.form, width=event.width))
        body.bind("<Configure>", lambda event: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        body.columnconfigure(0, weight=1)
        ttk.Label(body, text="Player settings", style="Section.TLabel").grid(row=0, column=0, sticky="w", pady=(8, 8))
        self.settings_frame = ttk.Frame(body, padding=12, style="Panel.TFrame")
        self.settings_frame.grid(row=1, column=0, sticky="ew")
        self.settings_frame.columnconfigure(1, weight=1)
        self.availability_label = ttk.Label(body, textvariable=self.availability, wraplength=500)
        self.availability_label.grid(row=2, column=0, sticky="w", pady=(8, 10))
        ttk.Label(body, text="Languages", style="Section.TLabel").grid(row=3, column=0, sticky="w", pady=(8, 8))
        language_frame = ttk.Frame(body, padding=12, style="Panel.TFrame")
        language_frame.grid(row=4, column=0, sticky="ew")
        language_frame.columnconfigure(0, weight=1)
        ttk.Label(language_frame, text="Installed language", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.language_box = ttk.Combobox(language_frame, textvariable=self.language_choice, state="readonly", width=32)
        self.language_box.grid(row=1, column=0, sticky="ew", pady=(5, 14))
        self.language_button = ttk.Button(language_frame, text="Use language", command=self.choose_language)
        self.language_button.grid(row=1, column=1, padx=(10, 0), pady=(5, 14))
        ttk.Label(language_frame, text="Add or replace a .pack file", style="Panel.TLabel").grid(row=2, column=0, columnspan=2, sticky="w")
        file_row = ttk.Frame(language_frame, style="Panel.TFrame")
        file_row.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(5, 8))
        file_row.columnconfigure(0, weight=1)
        self.path_entry = ttk.Entry(file_row, textvariable=self.path)
        self.path_entry.grid(row=0, column=0, sticky="ew")
        self.browse_button = ttk.Button(file_row, text="Choose file…", command=self.browse)
        self.browse_button.grid(row=0, column=1, padx=(8, 0))
        self.select_check = ttk.Checkbutton(language_frame, text="Use after installing", variable=self.select_after_install)
        self.select_check.grid(row=4, column=0, sticky="w", pady=(0, 10))
        self.install_button = ttk.Button(language_frame, text="Install pack", command=self.install)
        self.install_button.grid(row=4, column=1, padx=(10, 0), pady=(0, 10))
        ttk.Label(language_frame, text="Keep the USB cable connected while the player reconnects.", style="Panel.TLabel", wraplength=500).grid(row=5, column=0, columnspan=2, sticky="w")
        self.mutators = [self.language_button, self.path_entry, self.browse_button, self.select_check, self.install_button]
        self.readonly = [self.language_box]
        footer = ttk.Frame(root, padding=(20, 12, 20, 16))
        footer.grid(row=2, column=0, sticky="ew")
        footer.columnconfigure(0, weight=1)
        self.message_label = ttk.Label(footer, textvariable=self.message, wraplength=690, style="Accent.TLabel")
        self.message_label.grid(sticky="ew")
        root.bind("<FocusIn>", self.reveal_focus, add="+")
        root.bind("<Configure>", self.resize, add="+")
        self.start("refresh")
        self.root.after(60, self.tick)

    def _style(self):
        style = self.ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", background=BACKGROUND, foreground=TEXT, font=("Helvetica", 12))
        style.configure("TFrame", background=BACKGROUND)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("TLabel", background=BACKGROUND, foreground=MUTED)
        style.configure("Panel.TLabel", background=PANEL, foreground=MUTED)
        style.configure("Title.TLabel", foreground=TEXT, font=("Helvetica", 18, "bold"))
        style.configure("Section.TLabel", foreground=TEXT, font=("Helvetica", 13, "bold"))
        style.configure("Accent.TLabel", foreground=CYAN)
        style.configure("Error.TLabel", foreground=PINK)
        style.configure("TButton", padding=(10, 6), background=PANEL, foreground=TEXT,
                        bordercolor=MUTED, focuscolor=CYAN)
        style.map("TButton", foreground=[("disabled", "#758294")],
                  background=[("active", "#304459")], bordercolor=[("focus", CYAN)])
        style.configure("TCombobox", fieldbackground=PANEL, background=PANEL, foreground=TEXT,
                        arrowcolor=CYAN, padding=5)
        style.map("TCombobox", fieldbackground=[("readonly", PANEL), ("disabled", BACKGROUND)],
                  foreground=[("disabled", "#758294"), ("readonly", TEXT)],
                  selectbackground=[("readonly", PANEL)], selectforeground=[("readonly", TEXT)])
        style.configure("TEntry", fieldbackground=PANEL, foreground=TEXT, insertcolor=CYAN, padding=5)
        style.configure("TCheckbutton", background=PANEL, foreground=TEXT, focuscolor=CYAN)
        style.map("TCheckbutton", background=[("active", PANEL)], foreground=[("disabled", "#758294")])
        self.root.option_add("*TCombobox*Listbox.background", PANEL)
        self.root.option_add("*TCombobox*Listbox.foreground", TEXT)
        self.root.option_add("*TCombobox*Listbox.selectBackground", "#304459")

    def resize(self, event):
        if event.widget == self.root:
            width = max(500, event.width - 60)
            self.message_label.configure(wraplength=width)
            self.details_label.configure(wraplength=width)
            self.availability_label.configure(wraplength=max(420, event.width - 90))

    def reveal_focus(self, event):
        widget = event.widget
        if not str(widget).startswith(str(self.settings_frame).rsplit(".", 1)[0]):
            return
        try:
            self.root.update_idletasks()
            top = widget.winfo_rooty() - self.canvas.winfo_rooty()
            height = self.canvas.winfo_height()
            bounds = self.canvas.bbox("all")
            if bounds and bounds[3] > height and (top < 0 or top + widget.winfo_height() > height):
                position = self.canvas.canvasy(0) + top - 12
                self.canvas.yview_moveto(max(0, position) / bounds[3])
        except self.tk.TclError:
            pass

    def start(self, action, revision=None, **arguments):
        revision = self.view_revision if revision is None else revision
        if self.controller.submit(action, revision=revision, **arguments):
            self.message.set("Reading USB player…" if action == "refresh" else
                             "Applying change… Keep the cable connected while the player reconnects.")
            self.message_label.configure(style="Accent.TLabel")
        elif self.controller.error:
            self.show_error(self.controller.error)
        self.enable_controls()

    def browse(self):
        if self.controller.busy or self.controller.closing:
            return
        path = self.filedialog.askopenfilename(parent=self.root, title="Choose a language pack",
                                               filetypes=[("Language pack", "*.pack"), ("All files", "*")])
        if path:
            self.path.set(path)

    def install(self):
        self.start("install", path=self.path.get(), select=self.select_after_install.get())

    def choose_language(self):
        index = self.language_box.current()
        if 0 <= index < len(self.languages):
            self.start("language", code=self.languages[index]["code"])

    def show_error(self, error):
        self.message.set(error["message"] + " (" + error["code"] + ")")
        self.message_label.configure(style="Error.TLabel")

    def render(self):
        self.view_revision = self.controller.revision
        snapshot = self.controller.snapshot or {}
        connected = snapshot.get("connected", False)
        self.summary.set("Connected · USB storage" if connected else "Player not connected")
        values = (snapshot.get("report") or {}).get("values", {})
        build = values.get("firmware.build") or (snapshot.get("settings") or {}).get("firmware_build")
        percent = values.get("power.percent_shown")
        battery = str(percent) + "%" if type(percent) is int and 0 <= percent <= 100 else "unknown"
        self.details.set(("Build " + str(build or "unknown") + "  ·  Battery " + battery + " · Last USB snapshot") if connected else
                         "Connect in USB storage mode, then choose Refresh.")
        for widget in self.settings_frame.winfo_children():
            widget.destroy()
        self.setting_rows = []
        entries = (snapshot.get("settings") or {}).get("settings", [])
        for index, entry in enumerate(entries):
            self.ttk.Label(self.settings_frame, text=entry["label"], style="Panel.TLabel", wraplength=150).grid(row=index, column=0, sticky="w", padx=(0, 12), pady=5)
            options = entry["options"]
            box = self.ttk.Combobox(self.settings_frame, state="readonly", width=24,
                                   values=[option["label"] + " · " + option["value"] for option in options])
            box.grid(row=index, column=1, sticky="ew", pady=5)
            current = next((i for i, option in enumerate(options) if option["value"] == entry["value"]), -1)
            if current >= 0:
                box.current(current)
            def apply(entry=entry, box=box, options=options, revision=self.view_revision):
                if revision != self.controller.revision:
                    return
                chosen = box.current()
                if 0 <= chosen < len(options):
                    self.start("setting", revision=revision, setting=entry["key"], value=options[chosen]["value"])
            button = self.ttk.Button(self.settings_frame, text="Apply", command=apply)
            button.grid(row=index, column=2, padx=(10, 0), pady=5)
            self.setting_rows.append((box, button))
        if not entries:
            self.ttk.Label(self.settings_frame, text="Settings unavailable", style="Panel.TLabel").grid(sticky="w")
        notices = [value["message"] for key, value in snapshot.get("errors", {}).items() if key != "status"]
        self.availability.set("\n".join(notices))
        self.languages = (snapshot.get("languages") or {}).get("languages", [])
        self.language_box.configure(values=[entry["name"] + " · " + entry["code"] for entry in self.languages])
        selected = (snapshot.get("languages") or {}).get("selected")
        index = next((i for i, entry in enumerate(self.languages) if entry["code"] == selected), -1)
        if index >= 0:
            self.language_box.current(index)
        else:
            self.language_choice.set("")
        if self.controller.error:
            self.show_error(self.controller.error)
        else:
            self.message.set("Change completed and player state refreshed." if self.controller.last_action != "refresh" else
                             ("Ready. Choose a setting or language." if connected else "Waiting for a USB storage connection."))
            self.message_label.configure(style="Accent.TLabel")
        self.enable_controls()

    def enable_controls(self):
        ready = not self.controller.busy and not self.controller.closing
        connected = bool((self.controller.snapshot or {}).get("connected"))
        self.refresh_button.configure(state="normal" if ready else "disabled")
        for widget in self.mutators:
            widget.configure(state="normal" if ready and connected else "disabled")
        self.language_button.configure(state="normal" if ready and connected and self.languages else "disabled")
        self.language_box.configure(state="readonly" if ready and connected and self.languages else "disabled")
        for box, button in self.setting_rows:
            box.configure(state="readonly" if ready and connected else "disabled")
            button.configure(state="normal" if ready and connected else "disabled")

    def close(self):
        if self.controller.request_close():
            self.root.destroy()
        else:
            self.message.set("Finishing the USB operation before closing. Keep the cable connected.")
            self.enable_controls()

    def tick(self):
        if self.controller.poll():
            if self.controller.closed:
                self.root.destroy()
                return
            self.render()
        if self.root.winfo_exists():
            self.root.after(60, self.tick)


def launch(backend=None):
    try:
        import tkinter as tk
        from tkinter import filedialog, ttk
    except ImportError:
        print("This Python has no Tk. Use a Python with Tk compatible with your macOS, or use tools/echo_debug.py for USB controls.", file=sys.stderr)
        return 1
    try:
        root = tk.Tk()
    except tk.TclError as error:
        print("The desktop window could not open: " + str(error), file=sys.stderr)
        return 1
    Window(root, tk, ttk, filedialog, backend if backend is not None else echo_debug.Tool())
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(launch())
