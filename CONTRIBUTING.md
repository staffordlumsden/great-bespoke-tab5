# Contributing

## Before changing firmware

Read `README.md`, `AGENTS.md`, the current Console checkpoint and the affected
project README. Preserve its `sdkconfig`, `dependencies.lock`, local HAL and
third-party notices.

Device operations are separate from source changes. Do not flash, erase NVS,
format storage, update the factory C6 image or alter a physical device unless the
task explicitly authorises that operation.

## Build and test

Use ESP-IDF v5.5.5 through the workspace `idf.py` wrapper. Configure host paths
only in `.tools/export.local.sh`.

Run the affected project's native tests, then its firmware build. Record physical
acceptance separately from host/build results.

Console checks:

```sh
cd projects/tab5-console
bash tests/run-checks.sh
../../idf.py build
```

## Public-release scan

Before committing or pushing:

```sh
python3 tools/check_public_release.py
```

The check rejects generated trees, recovery material, host-specific paths and
obvious token formats. `gitleaks` may be used as a second scanner.

## Commit boundaries

- Commit source, tests, dependency locks, project configuration and reviewed
  documentation.
- Do not commit SDKs, managed components, build output, raw logs, NVS/full-flash
  dumps, SD images, credentials, private keys, local profiles or machine-local
  environment files.
- Use placeholders such as `/dev/cu.usbmodemXXXX`, `<SD_MOUNT>` and `<LAN_IP>`.
- Do not add proprietary fonts or other binary assets without a redistribution
  licence. New third-party material needs its source, version and licence.
- Use public GitHub issue content for defects only; never publish live secrets or
  personal data in an issue, commit or test fixture.

By intentionally submitting a contribution, you agree that it may be distributed
under the licence applicable to the file or project being changed.
