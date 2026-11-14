#include "portal_internal.h"
#include <stddef.h>

/*
 * Common zones as POSIX TZ strings so newlib handles daylight saving.
 * Sign convention is inverted from the usual notation: UTC+10 is "UTC-10".
 */
static const portal_tz_t TZ_TABLE[] = {
    { "UTC",                       "UTC0" },
    { "London, Dublin, Lisbon",    "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Berlin, Paris, Madrid, Rome", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Athens, Helsinki, Kyiv",    "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Istanbul",                  "<+03>-3" },
    { "Moscow",                    "MSK-3" },
    { "Dubai",                     "<+04>-4" },
    { "Tehran",                    "<+0330>-3:30" },
    { "Karachi",                   "PKT-5" },
    { "India (Kolkata)",           "IST-5:30" },
    { "Kathmandu",                 "<+0545>-5:45" },
    { "Dhaka",                     "<+06>-6" },
    { "Bangkok, Jakarta",          "<+07>-7" },
    { "China, Singapore, Perth",   "<+08>-8" },
    { "Hong Kong",                 "HKT-8" },
    { "Japan, Korea",              "JST-9" },
    { "Adelaide",                  "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Darwin",                    "ACST-9:30" },
    { "Brisbane",                  "AEST-10" },
    { "Sydney, Melbourne, Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Auckland",                  "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Azores",                    "<-01>1<+00>,M3.5.0/0,M10.5.0/1" },
    { "Sao Paulo",                 "<-03>3" },
    { "Buenos Aires",              "<-03>3" },
    { "Newfoundland",              "NST3:30NDT,M3.2.0,M11.1.0" },
    { "Halifax (Atlantic)",        "AST4ADT,M3.2.0,M11.1.0" },
    { "New York (Eastern)",        "EST5EDT,M3.2.0,M11.1.0" },
    { "Chicago (Central)",         "CST6CDT,M3.2.0,M11.1.0" },
    { "Denver (Mountain)",         "MST7MDT,M3.2.0,M11.1.0" },
    { "Phoenix (no DST)",          "MST7" },
    { "Los Angeles (Pacific)",     "PST8PDT,M3.2.0,M11.1.0" },
    { "Anchorage",                 "AKST9AKDT,M3.2.0,M11.1.0" },
    { "Honolulu",                  "HST10" },
    { "Mexico City",               "CST6" },
    { "Johannesburg",              "SAST-2" },
    { "Lagos",                     "WAT-1" },
    { "Nairobi",                   "EAT-3" },
};

const portal_tz_t *portal_tz_table(size_t *count)
{
    *count = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);
    return TZ_TABLE;
}
