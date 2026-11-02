# Security policy

## Supported versions

Only the current `main` branch gets security fixes.

## Reporting a vulnerability

Report privately through GitHub. Open the repository's **Security** tab and choose **Report a vulnerability**. Do not open a public issue for a security problem.

Include the commit you tested, your macOS version and Mac model, and the steps to reproduce. A proof of concept helps but is not required.

## Scope

A report matters most for:

- `bin/tm2020`, which downloads Ubisoft's Connect installer over HTTPS. Ubisoft replaces that file over time, so it has no pinned checksum.
- `tools/build-runtime.sh`, which clones TrackMania-MacOS at a pinned commit and runs its builder.
- The Wine and D9VK patches under `patches/`.

Trackmania, Ubisoft Connect and Openplanet are not part of this repository, so please report flaws in them to their vendors. For flaws in Wine, DXVK, MoltenVK or the libraries built with them, please coordinate with those projects as well.

Please do not attach logs without reading them first. Connect's logs can contain signed download URLs and account details.
