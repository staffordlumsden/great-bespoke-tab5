#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/ical_lines.cpp tests/test_ical_lines.cpp -o "$tmp_dir/test-ical-lines"
"$tmp_dir/test-ical-lines"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/calendar_files.cpp tests/test_calendar_files.cpp -o "$tmp_dir/test-calendar-files"
"$tmp_dir/test-calendar-files"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/calendar_view.cpp tests/test_calendar_view.cpp -o "$tmp_dir/test-calendar-view"
"$tmp_dir/test-calendar-view"
echo 'Calendar native tests passed'
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/calendar_view.cpp src/ical_lines.cpp src/calendar_events.cpp tests/test_calendar_events.cpp -o "$tmp_dir/test-calendar-events"
"$tmp_dir/test-calendar-events"
echo 'Calendar event tests passed'
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/calendar_feeds.cpp tests/test_calendar_feeds.cpp -o "$tmp_dir/test-calendar-feeds"
"$tmp_dir/test-calendar-feeds"
echo 'Calendar feed configuration tests passed'
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/test_calendar_layout.cpp -o "$tmp_dir/test-calendar-layout"
"$tmp_dir/test-calendar-layout"
echo 'Calendar layout tests passed'
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/calendar_fetch.cpp tests/test_calendar_fetch.cpp -o "$tmp_dir/test-calendar-fetch"
"$tmp_dir/test-calendar-fetch"
echo 'Calendar feed retry tests passed'
