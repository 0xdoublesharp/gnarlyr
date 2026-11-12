# Desktop USB companion

The desktop companion reads the connected player's build and battery snapshot, changes supported settings, and installs or selects language packs. It uses the same checked USB operations as the [command-line companion](companion-usb.md).

From the repository root, launch it with a Python runtime whose Tk supports your installed macOS version:

```sh
python3 tools/echo_companion_app.py
```

Tk must support your installed macOS version. Python runtimes without Tk can still run the command-line tools; importing the desktop controller does not require Tk. The desktop companion needs no account, server or extra Python packages.

Connect the player in USB storage mode and choose **Refresh**. The build and battery are the last USB report snapshot. Choose a supported setting and press **Apply**, or choose an installed language and press **Use language**.

To add or replace a language, choose a local `.pack` file and press **Install pack**. **Use after installing** selects it after installation. The shared language helper checks the pack against the installed firmware and keeps a backup when replacing one. See [language packs](language-packs.md) for pack preparation.

Keep the USB cable connected while a change runs. Controls stay disabled until the player reconnects and the result is checked. A failed operation displays its error and is never retried automatically. Closing the window during an operation waits for it to finish; closing does not cancel a write.
