# Using CS OS

After [installing the host and firmware](SETUP.md), use the Cardputer keyboard and the TFT touchscreen to work in XFCE. The external screen shows the connection menu before you open Linux. During a session, Linux stays on the TFT and the Cardputer's built-in screen shows the controls. The labels below match the Italian firmware menus.

## Open your first session

1. Switch on the Cardputer. It tries a known Wi-Fi network automatically. If you need another one, select **Scegli rete** (Choose network), choose the network, and enter its password. Use the triangle keys or **Tab** to select a button on the connection screen, then **Enter** to activate it.
2. Wait for the connection checks. If GitHub confirms that the Codespace is stopped, choose **Avvia Codespace** (Start Codespace). Press **R** on the connection screen to check again.
3. Choose **Apri desktop** (Open desktop) when it is available. Wait for the first frame on the TFT. Tap **Opt** to switch between **Scrivi** (Type) and **Controlla** (Control).

## Find your way around XFCE

Open the applications menu on the panel, then tap an app to launch it. The default CS OS profile also provides launchers for **Terminale** (Terminal), **File** (File Manager), **Browser**, and **Cerca applicazioni** (Application Finder). Use the task list to switch windows or the workspace switcher to change workspaces.

To run a Linux command, open the terminal from the XFCE panel, tap **Opt** until the small display shows **Scrivi**, then type your command and press **Enter**. For example, `pwd` shows the current directory on the Codespace. The [optional desktop applications](SETUP.md#optional-desktop-applications) add document, image, PDF, media, and archive tools.

## Keyboard and touch reference

| Mode | Action | Command |
| --- | --- | --- |
| Either | Switch between typing and pointer controls | Tap **Opt**. |
| Either | Open local settings | Hold **Opt** for one second. Up/down selects, **Enter** opens, **Esc** goes back, and **Opt** closes the menu. |
| Either | Click with the stylus | Tap to left-click. Hold still for at least 0.6 seconds to right-click. Touch acts where you tap on the displayed frame. |
| **Scrivi** | Type or send keys to Linux | Type with **Fn** off. Tap **Fn** to toggle the orange functions. |
| **Scrivi** | Shift and blue symbols | Hold **Aa** for Shift, or tap it on its own for less than 350 ms to latch or unlatch Shift. |
| **Scrivi** | Navigation and function keys | With **Fn** on, the triangles are arrows, **[** and **]** are Page Up/Down, and **H**, **E**, **I** are Home, End, Insert. **1–9**, **0**, **-**, **=** send F1–F12. **P**, **M**, **Del**, **Esc** send Print Screen, Menu, Delete, Escape. With **Fn** off, the **Esc** key types a backtick. |
| **Scrivi** | Send Super | With **Fn** on, tap **Tab** to arm Super for the next input or hold **Tab** to keep Super pressed. Tap **Tab** again to cancel an armed modifier. |
| **Controlla** | Move the pointer | Use the triangle keys. **Aa** changes pointer precision. |
| **Controlla** | Click or hold a mouse button | Tap **Enter** for a left click on release or hold it for at least 0.6 seconds for a right click on release. Hold the apostrophe for middle button or backslash for right button. |
| **Controlla** | Drag | Press **Space** to start, move with the triangle keys, and press **Space** again to release. **Esc** also releases. |
| **Controlla** | Pan, zoom, scroll, or fit the whole desktop | **E/A/S/D** pan the view. **-** and **=** change zoom. **[** and **]** scroll. **Tab** switches between full-desktop and native views. |
| **Controlla** | Move a window from the keyboard | Focus the window, then press **F** to start XFCE's window move. Move with the orange triangles, press **Enter** to confirm, or **Esc** to cancel. |

Tap **Ctrl** or **Alt** to arm it for the next key, click, drag, or stylus tap. Tap again to cancel or hold the modifier to keep it pressed. Panning and zooming leave armed modifiers available. Holding **Aa** or using it with another key preserves the previous Shift latch when you release it. To send **Super** from settings, use the **Codespace** menu.

In a zoomed view, a yellow dot at the edge marks the pointer outside the visible area. Use the triangle keys to bring the pointer back, or pan with **E/A/S/D**. During a drag, use the triangles to move the pointer and drag past the edge of the view. During a drag, **E/A/S/D** leave the view in place. Remote touch is suspended in local menus, during a keyboard drag or window move, and while the TFT is off.

## Change display settings

Hold **Opt** and choose **Regola schermi** (Display settings). **Luce interna** adjusts the Cardputer screen and **Luce esterna** adjusts the TFT. Left/right changes brightness in 10% steps between 10% and 100%. The menu shows whether the setting was saved.

During a session, **TFT: spegni** blanks the TFT while keeping Linux connected. Use the small screen to return to **Regola schermi → TFT: accendi**. The same menu includes the FPS limit, image quality, cursor, next monitor, and **Aggiorna immagine** (Refresh image). In the FPS setting, **SRV** follows the server limit and **MAX** removes the Cardputer's own cap.

## Use audio and the microphone

During a session, hold **Opt** and choose **Codespace → Audio e microfono**. Select playback or microphone with up/down, toggle it with **Enter**, and use the printed comma and slash keys to adjust volume or boost. The microphone starts off for each new session. **ON attesa** means the microphone is waiting and **ON Linux** means it is active on the host.

MIC and OUT boost range from 0 to 20 dB. MIC 0 dB is unity gain. Actual OUT boost may be lower when the signal has limited headroom. Audio can drop out during desktop use.

**Salva prova PC** (Save PC test) appears only on builds with the USB console enabled and requires an active serial monitor. It records microphone and speaker-reference data after you select it. Select it again or turn off the microphone to stop the capture.

## Disconnect, stop, or reconnect

- To leave Linux running, hold **Opt** and choose **Codespace → Disconnetti** (Disconnect).
- To stop the host, save your work in XFCE, then choose **Codespace → Arresta Codespace** (Stop Codespace) and confirm. Running apps and processes close. Switching off the Cardputer does not stop the Codespace immediately.
- If the desktop connection drops, return to the connection screen and open **Apri desktop** once it is ready. Press **R** to check status, or use **Verifica collegamenti** (Check connections) in settings. A status check does not start or stop the Codespace. If a Wi-Fi switch fails, cancel it to recover the previous network and wait for recovery to finish.

## Find help on the Cardputer

Hold **Opt** and choose **Guida e diagnostica → Guida ai comandi**. Use up/down to choose a topic, **Enter** or right for the next page, left for the previous page, and **Esc** to go back. The same menu contains **Informazioni** (Information) and **Log eventi** (Event log), which help distinguish Wi-Fi, GitHub, and desktop connection problems.

| If you get stuck | Where to look |
| --- | --- |
| The Cardputer cannot join Wi-Fi | Open **Scegli rete** or **Reti e accesso → Reti Wi-Fi**. Press **R** in the network list to scan again. |
| Wi-Fi works but the desktop is unavailable | Check the Codespace name and GitHub token in **Reti e accesso → GitHub / Codespace**. Check the bridge user, password, and PIN in **Accesso desktop**. On the host, verify that port 8089 forwarding is enabled in the [Codespaces setup](SETUP.md#codespaces-host). A WSS 404 alone does not tell you whether the Codespace is stopped. |
| The desktop is connected but keys move the pointer instead of typing | Tap **Opt** until **Scrivi** appears. Turn **Fn** off for ordinary typing. |
| The built-in screen works but the TFT is dark | Check **Regola schermi → TFT: accendi**. If the TFT has never shown the connection screen, check the [V2 wiring diagram](CABLAGGIO-V2.md). |
