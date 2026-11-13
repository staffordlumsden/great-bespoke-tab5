# Tab5 Calendar 0.1.2-dev

An SD-installed calendar application for the Great Bespoke Tab5. This
development build combines local `.ics` files with Google and Outlook iCalendar
subscriptions, and uses the Console dark palette and PixelFonts. On 13 November
2026, the user confirmed the Tab5 firmwares, including Calendar, work as
intended. The factory Console remains the launcher.

## Calendar setup

On the SD card create `/files/Calendar/`. Put any `.ics` exports there. Each
file becomes a separate calendar. These files are snapshots; replace them when
you want an updated export. Hidden files, folders and non-ICS files are ignored.

For Google or Outlook subscriptions, create `/files/Calendar/feeds.txt` as plain UTF-8
text, with one name and **iCal** address separated by `|` on each line:

```text
Personal|https://calendar.google.com/calendar/ical/REPLACE_WITH_FIRST_ICAL_ADDRESS
Work|https://calendar.google.com/calendar/ical/REPLACE_WITH_SECOND_ICAL_ADDRESS
Other|https://calendar.google.com/calendar/ical/REPLACE_WITH_THIRD_ICAL_ADDRESS
```

Use the complete **Public address in iCal format** from each Google Calendar's
settings. Browser-view and sharing URLs are not iCalendar feeds. Replace each
example URL in its entirety, leave no spaces around the separator and use a
unique calendar name. Do not add the same calendar as both a file and a feed
unless duplicate events are desired. An Outlook `.ics` export can be added in
this folder when available. The client accepts HTTPS feed URLs hosted at
`calendar.google.com/calendar/ical/` or `outlook.office365.com/owa/calendar/`.

The app reconnects the Wi-Fi station saved by Console. Connect to Wi-Fi in
Console before launching. It retrieves network feeds after the system clock has
been synchronised, then refreshes every 30 minutes. Temporary download failures
are retried up to three times, and failed feeds are tried again after one minute. `R` reloads SD files and
starts another refresh. On a failed refresh the last successful feed data remains
in memory until the app exits. Feed URLs and calendar content are not bundled
with the firmware. Calendar feeds can reveal the information they publish;
keep the config file private when appropriate.

## Controls

- **MONTH**, **WEEK**, **AGENDA**, and **CALENDARS** select the views by touch;
  keyboard keys `1` through `4` do the same.
- Month cells show two event previews and a count when there are more. Select a
  date with touch or arrow keys. **PREV/NEXT** changes month.
- Week view uses left/right to choose a day, up/down to scroll crowded days,
  and **PREV/NEXT** to change week.
- Agenda lists visible events in time order for the next 366 days from the
  selected date. Arrows scroll; left/right move seven rows. Select another date
  in month view to start elsewhere. A 3,000-row cap is explicitly indicated.
- In **CALENDARS**, each calendar shows its download or parsing status. HTTP,
  network/TLS, incomplete-download, and unsupported-event errors are reported
  separately. Tap a name to toggle visibility and tap **COLOUR** to cycle
  the Console's red, yellow, green, blue and purple. Keyboard up/down selects a
  calendar, space or Enter toggles it, and left/right changes colour. Settings
  persist by calendar name.
- **CONSOLE** returns to the resident launcher.

## Calendar format limits

The reader supports all-day and timed events, UTC and Sydney/Melbourne times,
Seoul and Bogota fixed-offset times since 2000, common recurrence rules,
exceptions and changed individual occurrences, and date or date-time RDATE entries.
The display time zone is Sydney. It does **not** implement the full iCalendar
standard: other time zones, RANGE exceptions, duration-valued RDATE and complex
recurrence rules can be unsupported. A warning indicates that events or files
were rejected; do not rely on this development build as the sole appointment
record. Files and feeds are limited to 4 MiB, 8,192 total events, and 16
calendars (up to eight network feeds).

## Build and install

The [0.1.2-dev pre-release](https://github.com/staffordlumsden/great-bespoke-tab5/releases/tag/tab5-calendar-0.1.2-dev)
contains the matching image, manifest and licence for the Console SD library.

```sh
bash tests/run-native.sh
cd firmware
../../../idf.py build
cd ..
python3 tools/package_sd_app.py --image firmware/build/Tab5Calendar.bin \
  --output artifacts/sd-library --license Tab5Calendar-LICENSE.txt
```

Copy the three matching release files (or the generated files from
`artifacts/sd-library/`) to `/firmware/` on SD, then install using Console's SD
App Library. Direct flash targets are
blocked. Do not flash this application at the factory Console offset.

Host sanitizer tests and real calendar parser checks passed on 13 November
2026. The user's general firmware acceptance is recorded in
`../../docs/TAB5_ACCEPTANCE.md`; earlier evidence is in
`../../docs/TAB5_CALENDAR_DEVELOPMENT.md`.
