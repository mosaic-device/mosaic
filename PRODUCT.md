# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Undecided. The marketing site is in visual design only; no code yet. The repo's web apps use Vite, React and Tailwind.

## Users

Developers who live in GitHub, Slack, Jira, Discord and Google Calendar and keep getting pulled out of their editor by notifications. Secondary: makers who want to build the device themselves from the open hardware and firmware, and people setting up their own server.

## Product Purpose

Mosaic is a connected desk display. It brings the notifications that need you from many services together on one 5-inch screen beside your computer, so you can glance at them, dismiss them with a twist of the knob, or act on them (approve a PR, re-run CI, join a meeting) without switching windows.

## Positioning

Many sources, one view, on your desk and off your screen. You host the server yourself, so your tokens and activity never pass through anyone else's servers. Everything is open source and the device is build-it-yourself.

## Operating Context

- The device sits on a desk next to a laptop or monitor, glanced at during work.
- A self-hosted .NET server connects to the services and pushes notifications to the device over a WebSocket.
- A web admin app connects accounts and pairs devices.
- A desktop companion app runs on the laptop, so an action on the device can open the PR in the browser or the file in the editor.
- Setup: the device opens a Wi-Fi setup page reached from a phone, then pairs with the server using a code shown in the admin app.

## Capabilities and Constraints

- Hardware: ESP32-P4 (Waveshare ESP32-P4-WIFI6-Touch-LCD-5), 5" touch display, Wi-Fi 6, a rotary knob with a push button, an ambient light sensor, an LED ring and an LED strip, a speaker, and a custom carrier PCB.
- Integrations for the finished product: GitHub, Slack, Jira, Discord, Google Calendar.
- Self-hosted only; no hosted service. Build-only; not sold.
- The site describes the finished product (all milestones done), not today's state.

## Brand Commitments

- Name: Mosaic. The name means many pieces combined into one view, like a collage.
- Design tokens in `platform/packages/tokens` are binding, so the site matches the admin app and the device. Dark is the approved appearance; light is unapproved.
- Fonts from `@mosaic/assets`: Manrope for UI and display; Outfit 500 for the MOSAIC wordmark only.
- Primary color is purple `#7540B5`; status colors communicate status, not brand accents.

## Evidence on Hand

- Real device screens from the firmware preview (`pnpm fw:preview --state <state> --screenshot <file>`).
- KiCad renders of the carrier PCB in `hardware/pcb/renders/`.
- The wordmark SVG in `platform/packages/assets/web/mosaic-wordmark.svg`.
- No professional product photos and no enclosure model yet (planned for M6). No testimonials, users or metrics; don't invent them.

## Product Principles

- Calm over noisy: the device reduces interruption, so its presentation should too.
- Many pieces, one view: show sources coming together, not a list of features.
- Yours end to end: self-hosted, open source, built by you.
- Show the real thing: device screens and renders over stock imagery.
