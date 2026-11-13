# Setup and build

**Required hardware:** Cardputer ADV with the [V2 TFT and touch wiring](CABLAGGIO-V2.md). Check the pinout before flashing.

## Codespaces host

The devcontainer installs XFCE, Xvfb, PulseAudio, and the server. It applies the CS OS desktop profile during the first user session.

### GitHub secrets and token

1. In your GitHub account settings, open **Settings → Codespaces → New secret**. For each secret, select the repository from which you will create the Codespace under **Repository access**. [GitHub guide to Codespaces secrets](https://docs.github.com/en/codespaces/managing-your-codespaces/managing-your-account-specific-secrets-for-github-codespaces)
2. Create `CSOS_BRIDGE_USER`, `CSOS_BRIDGE_PASSWORD`, and `CSOS_CXL_PIN` with your own values. Use the same values in the Cardputer's local configuration. For `CSOS_GH_TOKEN`, you can use a classic personal access token with the `codespace` scope, issued by the account that manages the Codespace. Also set `CSOS_GH_TOKEN_EXPIRES` to `YYYY-MM-DD`, or set `CSOS_GH_TOKEN_NO_EXPIRY=1` for a token without an expiration date. [Codespaces API authentication](https://docs.github.com/en/rest/codespaces/codespaces)
### Create and start the Codespace

1. Open the repository on GitHub and select **Code → Codespaces**, then create a Codespace for the branch you want to install. Wait for the devcontainer setup to finish. If you added secrets after creating the Codespace, stop and restart it. [GitHub guide to creating a Codespace](https://docs.github.com/en/codespaces/developing-in-a-codespace/creating-a-codespace-for-a-repository)
2. In the Codespace terminal, run `printenv CODESPACE_NAME` and note the result for the Cardputer configuration.
3. Direct connections from the Cardputer use public forwarding on port 8089. Enable it with the command below if you want to connect over the internet. The bridge still requires your credentials and the CXL PIN. If you set `CSOS_EXPOSE_PUBLIC=1` as a Codespaces secret before the first setup, the bootstrap can enable forwarding for you.

```bash
CSOS_EXPOSE_PUBLIC=1 python3 tools/provision-host.py
bash .devcontainer/start.sh
python3 .devcontainer/exposure.py status
```

Provisioning preserves existing runtime settings. If you already provisioned the host with forwarding disabled, run `python3 .devcontainer/exposure.py resume` to enable it. To close remote access and keep it disabled across restarts, use `python3 .devcontainer/exposure.py pause`.

### Desktop profile and reset

During the first XFCE session, CS OS applies `desktop/profile.json` and records that the profile has been initialized for the user. Later restarts preserve user preferences. The initial panel provides the applications menu, launchers, task list, workspace switcher, show-desktop shortcut, home directory menu, system tray, audio, clock, and session actions.

To restore CS OS-managed desktop settings, open a terminal **inside the XFCE desktop** and run the following from the repository root:

```bash
bash desktop/apply-profile.sh --restore
```

Restart the desktop session so GTK settings read at startup are reapplied. To record package versions when reporting an issue:

```bash
dpkg-query -W xfce4-session xfce4-panel xfwm4 greybird-gtk-theme xvfb pulseaudio libpulse0 supervisor
```

### Optional desktop applications

To install the desktop apps used during development, run this from the repository root in a Codespace terminal:

```bash
bash desktop/install-daily-apps.sh
```

The script installs LibreOffice Writer, Calc, and Impress with Italian-language support, Atril, Ristretto, Drawing, Parole, archive tools, a calculator, and a screenshot utility. APT shows the required disk space before confirmation. The optional installer leaves file associations under user control.

### Updating the server after source changes

At startup, CS OS compares the eight runtime server files with the checkout and reports differences even if Supervisor is already running. To apply an update, run the following from a host terminal:

```bash
supervisorctl -c .devcontainer/supervisord.conf shutdown
bash .devcontainer/sync-server.sh --update
bash .devcontainer/start.sh
```

If Supervisor is already stopped, begin with the second command. Restarting the services interrupts the active desktop session. Runtime secrets and settings remain in their existing location.

## Cardputer ADV client

On your computer, clone or download the repository. Install Arduino CLI, PowerShell 7, and Python. From the root of your local copy, run these commands in PowerShell:

```powershell
pwsh tools/setup-arduino.ps1
New-Item -ItemType Directory -Force .local | Out-Null
Copy-Item config/device.example.json .local/device.json
```

Copy the example only when creating a new configuration. Open `.local/device.json` in a text editor and fill in the following fields.

| Fields | Values |
| --- | --- |
| `wifi_ssid`, `wifi_password` | The Wi-Fi network the Cardputer will use. |
| `codespace_name`, `wss_host` | Use the name printed by `printenv CODESPACE_NAME`. Set the host to `<codespace-name>-8089.app.github.dev`, without a protocol prefix or URL path. |
| `bridge_user`, `bridge_password`, `cxl_pin` | Match the host's `CSOS_BRIDGE_USER`, `CSOS_BRIDGE_PASSWORD`, and `CSOS_CXL_PIN`. |
| `github_token` | Use a token with access to manage your Codespace. You can use the token configured for the host. |

```powershell
python tools/configure.py --config .local/device.json
pwsh tools/build.ps1 -ConfigFile .tools/arduino/arduino-cli.yaml
```

Keep `.local/device.json`, the generated private headers, and your firmware binaries private. The firmware can embed your credentials. For a compile check with placeholders, use `-ExamplesOnly`.

The configuration uses 8 MiB flash, custom partitions, disabled PSRAM, and ESP32 core 3.2.1. For a blank device, use the bootloader, partition table, and application generated together. Before updating only the application, make sure the existing bootloader and partition layout are compatible. Keep a backup before replacing firmware.

## Flashing the Cardputer

1. Make a backup if the device contains settings or firmware you need to preserve. **Treat personal flash backups as sensitive: they can contain Wi-Fi, bridge, CXL, and GitHub credentials. Do not publish them.**
2. With the device switched OFF, hold G0, connect the Cardputer to your computer using a USB-C data cable, and release G0. Identify the new COM port. [M5Stack instructions](https://docs.m5stack.com/en/arduino/m5cardputer/program)
3. For initial installation, write the combined image produced by your configured build. Install esptool and replace `COM_PORT` with the detected port.

```powershell
python -m pip install esptool
python -m esptool --chip esp32s3 --port COM_PORT write-flash 0x0 csos-client/build/csos-client.ino.merged.bin
```

The combined image includes the bootloader and other selected flash regions, which may overlap with previously stored settings. **Writing an image is not a guarantee of a full-chip erase.** To update only the application, when the installed bootloader and partitions are compatible, use:

```powershell
python -m esptool --chip esp32s3 --port COM_PORT write-flash 0x10000 csos-client/build/csos-client.ino.bin
```

After flashing, restart the device normally with G0 released. Follow the [first-session guide](CONTROLLI.md#open-your-first-session) to connect and open XFCE.

## USB and diagnostics

The normal build disables USB CDC at boot, CS OS serial logs, the FPS probe, and CXL traces. Lightweight diagnostics remain available on the device's screen. To read logs, rebuild using `-UsbConsole` and open a serial monitor at 115200 baud. The options `-DiagnosticTelemetry`, `-FpsProbe`, `-CalibrationLab`, and `-TouchStudy` enable the console for their respective tests.

For firmware updates or recovery, use the Cardputer's download mode. The ROM bootloader's USB port may have a different identifier from the application console.

## Verifying a fresh installation

1. Check that provisioning starts the services and that the desktop shows the expected XFCE theme and panel.
2. Connect the Cardputer running the normal build. Check the first frame, keyboard, and touch input.
3. Move a window and check whether sharpness returns afterward. Test audio, disconnect the client, and confirm that it reconnects.
4. Record the client and server versions and test conditions. Refer to the [system reference](SYSTEM.md).
