#pragma once
#include <cstddef>
namespace calendar {
constexpr size_t max_feeds=8;
struct Feed { char name[100]; char url[1024]; };
struct FeedList { Feed feeds[max_feeds]; size_t count; };
// One 'name|https://...ics' per line, restricted to Google and Office365 hosts.
// Malformed input fails as a whole; never print the data in diagnostics.
bool parse_feeds(const char *data,size_t len,FeedList &out);
}
