"""Initialize or explicitly restore the CS OS desktop profile."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def set_property(channel, name, kind, *values, array=False):
    command = ["xfconf-query", "-c", channel, "-p", name, "--create"]
    if array:
        command.append("--force-array")
    for value in values:
        command.extend(("-t", kind, "-s", str(value).lower() if kind == "bool" else str(value)))
    subprocess.run(command, check=True)


def value_type(value, numeric_type):
    if isinstance(value, bool):
        return "bool"
    return numeric_type if isinstance(value, int) else "string"


def apply_profile(directory, home, config, restore):
    settings = config / "csos"
    marker = settings / "desktop-profile-initialized"
    if marker.is_file() and not restore:
        return

    profile = json.loads((directory / "profile.json").read_text(encoding="utf-8"))
    theme = home / ".themes" / profile["theme"]
    css = (directory / "CSOS-Greybird/gtk-3.0/gtk.css").read_text(encoding="utf-8")
    css = css.replace("@BASE_THEME@", profile["base_theme"])
    css = css.replace("@SCROLLBAR_PX@", str(profile["scrollbar_px"]))
    (theme / "gtk-3.0").mkdir(parents=True, exist_ok=True)
    (theme / "gtk-3.0/gtk.css").write_text(css, encoding="utf-8")
    gtk2 = theme / "gtk-2.0"
    if not gtk2.exists() and not gtk2.is_symlink():
        gtk2.symlink_to(Path("/usr/share/themes") / profile["base_theme"] / "gtk-2.0", target_is_directory=True)

    for channel, properties in (
        ("xsettings", {"/Net/ThemeName": profile["theme"], **profile["xsettings"]}),
        ("xfwm4", profile["xfwm4"]),
        ("xfce4-desktop", profile["desktop"]),
    ):
        for name, value in properties.items():
            set_property(channel, name, value_type(value, "int"), value)

    channel = "xfce4-panel"
    for name, value in profile["panel"].items():
        set_property(channel, f"/panels/panel-1/{name}", value_type(value, "uint"), value)

    ids = []
    for plugin_id, plugin in enumerate(profile["plugins"], start=101):
        ids.append(plugin_id)
        key = f"/plugins/plugin-{plugin_id}"
        name = plugin["name"]
        set_property(channel, key, "string", name)
        for prop, value in plugin.get("settings", {}).items():
            set_property(channel, f"{key}/{prop}", value_type(value, "uint"), value)
        if name == "directorymenu":
            set_property(channel, f"{key}/base-directory", "string", home)
        if name == "launcher":
            launcher_dir = config / "xfce4" / "panel" / f"launcher-{plugin_id}"
            launcher_dir.mkdir(parents=True, exist_ok=True)
            items = [f"{item}.desktop" for item in profile["launchers"]]
            for item in items:
                shutil.copyfile(directory / "launchers" / item, launcher_dir / item)
            set_property(channel, f"{key}/items", "string", *items, array=True)
    set_property(channel, "/panels/panel-1/plugin-ids", "int", *ids, array=True)
    set_property(channel, "/panels", "int", 1, array=True)

    settings.mkdir(parents=True, exist_ok=True)
    (settings / "gtk-overlay-scrolling").write_text(
        f"{int(profile['gtk_overlay_scrolling'])}\n", encoding="utf-8"
    )
    marker.touch()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--restore", action="store_true", help="Reapply the managed desktop settings")
    args = parser.parse_args()
    home = Path.home()
    config = Path(os.environ.get("XDG_CONFIG_HOME") or home / ".config")
    apply_profile(Path(__file__).resolve().parent, home, config, args.restore)
