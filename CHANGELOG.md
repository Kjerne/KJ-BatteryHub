# Changelog

What changed in each KJ-BatteryHub release. The release workflow copies a version's section into its GitHub
release, so every version tag needs a section here, headed `## <version>`. Changes made in the original
BatteryHub are listed in its own releases.

## 1.2.0 (2026-11-22)

- **A panel instead of the menu**, in the same look as the maintainer's F1DockTimers app. A click (left or
  right) on the taskbar strip or a tray icon opens it. **Devices:** level, state, connection and time to empty
  for each device; click one for its history, right-click it (or its *...* button) to show or hide it, move
  it, rename it, set its battery alert or forget it. **History:** the history viewer inside the panel, with
  buttons for three devices and a list for the rest. **Settings:** everything the menu had, on four pages,
  saving the same settings as before. Esc or a click elsewhere closes it; the pin keeps it open.
- **Forget device:** removes an old device's battery history, own name, place and alert, and hides it.
- The hover card and the larger history window use the panel's colours and the built-in Noto Sans typeface.
- The app calls itself **KJ-BatteryHub**: panel, tooltips, the exe's details (publisher Kjerne) and the installer
  (Start menu shortcut, Apps list). Settings > General > About links to this fork and to the original BatteryHub.
  The exe keeps its name, BatteryHub.exe, so updates and the startup entry keep working.
- **README rewritten for this fork:** what it changes, how updates work, and how to install it over the
  official BatteryHub. Donation, Discord and other links that belonged to the original project are gone.
- The button in Settings is now **Support the original project...**, and the README has a section about supporting
  the original project.
- The repository page no longer shows the original project's Sponsor button, and releases no longer post to
  the original project's Discord.

## 1.1.2 (2026-11-22)

- **Updates come from this fork:** *Check for updates* installs only KJ-BatteryHub releases signed with this
  fork's own key, never the official BatteryHub (which keeps only 30 days of history).
- Installer and portable zip, built by GitHub like the original project's, with HeadsetControl included.

## 1.1.1 (2026-11-22)

First release of the fork.

- **Battery history is kept for the device's lifetime.** The last 30 days keep every reading; older readings
  are thinned to the lowest and highest level per 6 hours, and readings older than a year to one pair per day.
- **History window:** a button per device, Last year, All time and Custom periods, dates under the chart, and
  the size of the saved history.
- **Left-click a device** on the taskbar or its tray icon to open its history (an option, on by default), and
  *Battery history...* in the main menu.
- **Hover card:** *Card chart period* setting (24 hours to 30 days, default 30), one time scale around "Now",
  and a "Click for full history" hint.
- **Tidier menu:** shorter device lines, details in each device's submenu, and receivers named the same way
  for every brand.
- One separator after *Display settings* instead of two.
