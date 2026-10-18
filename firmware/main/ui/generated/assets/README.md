# Generated Mosaic firmware assets

Generated offline with @mosaic/assets. Do not edit generated files.
Committed outputs are covered by platform/packages/assets/metadata/outputs.json; canonical
licenses remain in platform/packages/assets/licenses/.

Sources, pinned upstream URLs/revisions/checksums and generation tools are in
platform/packages/assets/. See that package's README.md for the explicit
pnpm generate-assets maintenance command and pinned Python environment setup.
Normal builds consume committed assets without running converters.

## Distribution notices

Keep this README and all three files in licenses/ with redistributed generated
asset source packages. For firmware binary/device releases, include these full
notices in the accompanying release documentation or third-party notices.
The build does not embed these text sidecars in the firmware image.

- mosaic_manrope_*.c: derived from Manrope, SIL Open Font License 1.1;
  copyright and full terms in licenses/Manrope-OFL.txt. Conversion changes
  weight/size, normalizes digit advances and rasterizes glyphs.
- mosaic_wordmark_*.c: outlined/rasterized Outfit 500 MOSAIC wordmark;
  source font copyright and SIL OFL 1.1 in licenses/Outfit-OFL.txt.
- mosaic_icon_settings_40.c: Lucide settings, release 0.468.0;
  copyright and full terms in licenses/Lucide-LICENSE.

These notices accompany the upstream-derived assets; they do not relicense
unrelated Mosaic code. Preserve the full notices when copying assets elsewhere.
