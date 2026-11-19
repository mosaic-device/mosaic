# Marketing site plan

Planning notes for the Mosaic marketing site. They describe the finished site, so details can change as the product does. Nothing is built yet. Once the content settles, the next step is a visual design, before any code.

## Decisions so far

- **Audience:** pitch to developers first; docs and a build guide sit behind the pitch for people who want to set one up or build one.
- **Framing:** describe the finished product (M4–M6 done), not today's state.
- **Open source:** firmware, server, apps and PCB.
- **Self-hosted only:** there is no hosted server. Everyone runs their own.
- **Build-only:** Mosaic isn't sold. The main call to action is building one.
- **Integrations:** GitHub, Slack, Jira, Discord and Google Calendar, presented as a full set.
- **Sections:** homepage, docs, build guide.
- **Look:** use `@mosaic/tokens` and the fonts and wordmark from `@mosaic/assets`, so it matches the admin app. Dark only: the light theme is unapproved.
- **Visual idea:** the name means many pieces combined into one view, like a collage. Notifications from different services come together on one screen. See [Visuals](#visuals).
- **Images come from renders, not photos.**

## Homepage

### 1. Hero

- Direction: **Collage**. Notification pieces from each service, cards and small chips, sit in two loose columns either side of the device, which shows one of them. Nothing overlaps the device.
- Headline: "Everything that needs you, in one view."
- "Many pieces. One view." is kept for further down the page.
- Subhead: "Mosaic is a 5-inch display with a knob that brings your GitHub, Slack, Jira, Discord and Calendar activity together on your desk, and lets you act on it without switching windows."
- Buttons: **Build your own** and **View on GitHub**. Docs is in the top navigation.
- Image: the device showing a real notification screen, such as a PR review request.

### 2. Why

One short paragraph: alerts keep pulling you out of the editor. Mosaic moves them off the laptop onto something you can glance at, dismiss with a twist, or act on with a tap.

### 3. What it does

Five blocks, each with a device screenshot or render.

- **Glance:** each notification shows where it's from, who sent it and what it's about, readable from your chair.
- **Act:** approve a PR, re-run a failed CI job, join the next meeting, without opening a browser.
- **Pick up on your laptop:** the companion app runs on your computer, so a press on the device can open the PR in your browser or the file in your editor.
- **Ambient:** the LED strip glows warm white behind the device while it's paired. The ring around the knob stays dark until a notification arrives, then turns blue and holds until you interact with the device, and the strip swells blue once. The ring turns amber when the server can't be reached. Nothing blinks or spins; every change fades. The light sensor dims everything at night.
- **Calm:** "Calm, until it matters." Turn the knob to scroll and set the volume, press it to dismiss. Quiet hours or a focus mode hold non-urgent notifications. The visual is a close-up of the knob, its ring dark, blue or amber.

### 4. Integrations

A grid of cards, each with what it shows and what you can do.

| Integration     | Shows                                          | You can                 |
| --------------- | ---------------------------------------------- | ----------------------- |
| GitHub          | Review requests, mentions, CI failures, merges | Approve a PR, re-run CI |
| Slack           | DMs and mentions                               | React, mark read        |
| Jira            | Issues assigned to you, status changes         | Move to In Progress     |
| Discord         | Mentions and DMs                               | Mark read               |
| Google Calendar | Your next meeting, with a countdown            | Join the call           |

### 5. How it works

- A diagram: services → **your server** → device, with the admin app beside the server and the companion app on your laptop beside the device.
- Main point: you host the server yourself, so your tokens and your activity never pass through anyone else's servers.
- Steps:
  1. Build the device.
  2. Run the server.
  3. Connect your accounts in the admin app.
  4. Pair the device, and install the companion app if you want laptop actions.

### 6. Hardware

Since Mosaic is build-only, this section leads into the build guide and gets more room than a usual spec list.

- Specs: ESP32-P4, 5" touch display, Wi-Fi 6, a rotary knob with a push button, an ambient light sensor, an LED ring and strip, and a speaker.
- Visual: an exploded view of the device's layers (case, display, ESP32-P4 board, carrier PCB, knob and LEDs) with labels, using the real PCB render and a real device screen. Headline idea: "Built by you, from open parts."
- What it takes: an off-the-shelf display board, a small carrier PCB, a few modules and a 3D-printed enclosure.
- Link: **Build your own →**

### 7. Open source

"Firmware, server, apps and the PCB are all on GitHub." Then the GitHub button again.

### Footer

Docs, Build guide, GitHub, License.

## Docs

- **Getting started:** what you need, running the server (Docker or a single binary), first-time setup, putting the device on Wi-Fi from your phone, pairing.
- **Integrations:** one page per service. Each covers connecting it, what triggers a notification, and the actions you get.
- **Companion app:** installing it, connecting it to your server, which actions open what on your laptop.
- **Using Mosaic:** notifications, knob and touch controls, what the light colors mean, sounds and volume, quiet hours, settings, factory reset.
- **Self-hosting:** reaching the server from outside your network (Tailscale Funnel), updates, backups, troubleshooting.

## Build guide

Parts list → order the carrier PCB → make the cables → wire it up → print and assemble the enclosure → flash the firmware → first boot.

Most of this already exists in [`hardware/pcb/README.md`](../hardware/pcb/README.md) and [`firmware/README.md`](../firmware/README.md). The site version needs to be written for someone who isn't working on the repo.

## Visuals

- **Mosaic motif:** pieces from different services come together into one view. It could show up as the hero assembling from tiles or the integration grid. Services have no colors of their own, on the screen or in the lights. Keep it a motif, not decoration on every section.
- **Device screens:** `pnpm fw:preview --state <state> --screenshot <file>` runs the real firmware and saves the screen. These are exact and can be regenerated as the UI changes.
- **The device:** there's no enclosure model until M6. Until then, draw the 5" panel and place a real screenshot inside it; switch to proper renders once the enclosure CAD exists.
- **PCB:** KiCad renders in `hardware/pcb/renders/`, made with `pnpm pcb:export`.
- **Diagrams:** drawn in code (SVG) with the tokens, for "How it works".
