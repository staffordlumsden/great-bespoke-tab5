# Tab5 Calendar 0.1.0-dev checkpoint

**Later update, 13 November 2026:** The user confirmed that all Tab5 firmwares,
including Calendar, work as intended. The entries below preserve the evidence
and limitations recorded at each earlier checkpoint; see
[`TAB5_ACCEPTANCE.md`](TAB5_ACCEPTANCE.md).

13 November 2026: Development candidate built as an SD-library app. It includes
Console-style month, week, agenda and per-calendar settings screens, local ICS
files plus Google iCal URL configuration, saved Console Wi-Fi, TLS CA-bundle
verification, a background fetch worker, the Console palette and keyboard
navigation. The source and usage details are in `projects/tab5-calendar/README.md`.

Native sanitizer checks passed for line unfolding, bounded file discovery,
date/navigation logic, visibility/colours, feed URL validation and a small set
of event/recurrence/time-zone examples. ESP-IDF v5.5.5 build succeeded. Package
validation accepted the ESP32-P4 16 MB image, 1,649,632 bytes, SHA-256
`feb363fdfe56fedc6de0ce2f42afa2784b38d15824bda5698d5f38fcf1999a95`.

This is **not physical acceptance**. Google feeds and real exports have not
been tested. Wi-Fi, TLS, SD installation, display, touch, keyboard, first-boot
confirmation, rollback and Console return all remain unverified on hardware.
The calendar reader supports only a subset of iCalendar, described in the
project README; unsupported data raises a warning, and recurrence overrides
may leave the original occurrence visible. The user must not treat it as the
sole appointment record. No device operation or flash was performed.

## 0.1.1-dev: recurrence and published Outlook support

Following the user's report of 31 unsupported events or files, the reader was
updated for Sunday and other week-start rules, modified and cancelled individual
occurrences, more monthly/yearly rules, multi-day recurrence, RDATE, longer
content lines and selected fixed-offset time zones. The file and total-event
limits were raised to 4 MiB and 8,192 respectively. The feed validator now
accepts the published Microsoft 365 Outlook ICS host as well as Google Calendar.

Host tests with sanitizers passed. The two supplied local exports parsed with
zero warnings (1,869 and 621 events); the household Google and published Outlook
feeds also parsed with zero warnings in host checks (621 and 217 events at the
time). The personal Google URL returned HTTP 404; the URL or publishing settings
need checking, so a local export was staged as a snapshot. No calendar content
or feed addresses are included in this checkpoint. Network behaviour varied
between attempts and must be tested on the Tab5.

The ESP-IDF v5.5.5 candidate built successfully and the package validator
accepted the ESP32-P4 16 MB image, 1,654,976 bytes, SHA-256
`1426ef0f3b5b15899d90e92401c9e80b5efc9859071179973bd1e3f9f54fe657`, minimum
chip revision 1.0. A local ignored SD-card staging tree contains the package,
the supplied personal snapshot and the other two feed addresses. No SD card or
device was modified. Physical Wi-Fi, calendar correctness, input, rendering,
installation and rollback remain unverified for this candidate. iCalendar support
is still bounded and partial; warnings must be investigated before relying on
the calendar as a complete record.

## 0.1.2-dev: feed diagnosis and larger controls

The user reported an Outlook unsupported warning and requested larger bottom
controls plus 21-pixel Agenda and Calendars text. Host checks found the published
Outlook feed readable and parseable with no unsupported items on the sample
retrieved. Independent host attempts also included a timeout, so a transient
connection problem is plausible but the reported on-device cause is unconfirmed.

The feed worker now retries transient transport, incomplete-download and selected
HTTP failures up to three times and uses a longer timeout. A failed refresh
keeps the previously loaded feed in memory and is retried after one minute.
Calendar rows identify HTTP errors, network/TLS errors, incomplete downloads,
invalid feeds and unsupported items separately; the footer no longer calls every
fetch failure unsupported. Feed URLs are not logged. The button rectangles grew
from 48 to 66 pixels high and their touch targets extend to the screen edges.
Agenda and Calendars primary text uses 21-pixel PixelFonts; long agenda month
names are abbreviated to fit.

Native sanitizer tests passed, including simulated retries, HTTP errors, empty
partial responses, button hit targets and text width. ESP-IDF v5.5.5 build and
SD package validation passed: 1,657,424 bytes, minimum ESP32-P4 revision 1.0,
SHA-256 `7b8bd7ff3a789d5bd1f86d215ba29f2338f334e3edce312cabbfd8678f5bd358`.
At this checkpoint the candidate had not yet been installed or physically
tested. Existing calendar files, settings and the Console were not changed.

### SD-card staging

The user subsequently connected the board in download mode and requested the
updated package on its SD card. Read-only serial identification reported an
ESP32-P4 revision v1.3. The mounted exFAT card's new 0.1.2-dev image, manifest
and licence were copied and independently matched against the local package.
The previous 0.1.1-dev package was moved into a dated directory under
`/firmware/rollback/`; other packages and calendar data were not changed.
The card was ejected cleanly. No application was flashed or installed to an OTA
slot during this operation: inserting the card and launching 0.1.2-dev through
At that point Console's SD App Library was the remaining installation step. The
user later confirmed that the Tab5 firmwares work as intended.
