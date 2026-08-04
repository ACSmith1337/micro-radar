#line 1 "/home/hermes/micro-radar/src/models/Aircraft.cpp"
#include "../models/Aircraft.h"

namespace JsonParser {
    template<>
    Aircraft Parse<Aircraft>(const JsonVariant& state) {
        Aircraft a;

        a.icao24 = state[0].isNull() ? "" : state[0].as<String>();
        a.callsign = state[1].isNull() ? "" : state[1].as<String>();
        a.originCountry = state[2].isNull() ? "" : state[2].as<String>();
        a.timePosition = state[3].isNull() ? 0 : state[3].as<long>();
        a.lastContact = state[4].isNull() ? 0 : state[4].as<long>();
        a.longitude = state[5].isNull() ? 0.0f : state[5].as<float>();
        a.latitude = state[6].isNull() ? 0.0f : state[6].as<float>();
        a.baroAltitude = state[7].isNull() ? 0.0f : state[7].as<float>();
        a.onGround = state[8].as<bool>();
        a.velocity = state[9].isNull() ? 0.0f : state[9].as<float>();
        a.trueTrack = state[10].isNull() ? 0.0f : state[10].as<float>();
        a.verticalRate = state[11].isNull() ? 0.0f : state[11].as<float>();
        // state[12] = sensors, skipped
        a.geoAltitude = state[13].isNull() ? 0.0f : state[13].as<float>();
        a.squawk = state[14].isNull() ? "" : state[14].as<String>();
        a.spi = state[15].isNull() ? false : state[15].as<bool>();
        a.positionSource = state[16].isNull() ? 0 : state[16].as<int>();
        a.category = state[17].isNull() ? 0 : state[17].as<int>();

        return a;
    }

    // ---------------------------------------------------------------------------
    // readsb / dump1090 aircraft.json parser
    //
    // readsb format: object-based JSON with named fields
    //   hex, callsign, alt/altitude, lat, lon, track, ground_speed, vertical_rate,
    //   on_ground, squawk, seen, seen_pos, rssi, geo_altitude
    //
    // dump1090-fa format: similar but some field name differences
    //   alt is "altitude" in readsb, dump1090-fa uses "alt" for baro and "altitude" for geo
    //   We try readsb fields first, then fall back to dump1090-fa fields
    // ---------------------------------------------------------------------------

    // Helper: get current unix time. ESP32 time may not be NTP-synced,
    // but readsb uses "seen" as seconds-ago relative to NOW, so we only
    // need a rough baseline for timePosition/lastContact.
    static long currentUnixTime() {
        return (long)time(nullptr);
    }

    AircraftReadsb ParseReadsbAircraft(const JsonVariant& ac) {
        AircraftReadsb a;

        // ICAO24 hex address
        a.icao24 = ac["hex"].isNull() ? "" : ac["hex"].as<String>();

        // Callsign — pad to 8 chars with spaces (standard convention)
        a.callsign = ac["callsign"].isNull() ? "" : ac["callsign"].as<String>();

        // readsb does not provide originCountry
        a.originCountry = "";

        // seen / seen_pos are seconds-ago, convert to unix timestamps
        const long seenPos = ac["seen_pos"].isNull() ? 0 : ac["seen_pos"].as<long>();
        const long seen = ac["seen"].isNull() ? 0 : ac["seen"].as<long>();
        const long now = currentUnixTime();
        a.timePosition = seenPos == 0 ? 0 : (now - seenPos);
        a.lastContact = seen == 0 ? 0 : (now - seen);

        // Position (WGS-84 decimal degrees)
        a.longitude = ac["lon"].isNull() ? 0.0f : ac["lon"].as<float>();
        a.latitude = ac["lat"].isNull() ? 0.0f : ac["lat"].as<float>();

        // Altitude: readsb "altitude" is in feet (barometric)
        // dump1090-fa: "alt" is barometric in feet, "altitude" is geometric in feet
        // Try "altitude" first (readsb), fall back to "alt" (dump1090-fa)
        float altFeet = 0.0f;
        if (!ac["altitude"].isNull()) {
            altFeet = ac["altitude"].as<float>();
        } else if (!ac["alt"].isNull()) {
            altFeet = ac["alt"].as<float>();
        }
        a.baroAltitude = altFeet * 0.3048f;

        // On ground
        a.onGround = !ac["on_ground"].isNull() ? ac["on_ground"].as<bool>() : false;

        // Ground speed: readsb/dump1090 use knots → m/s
        // "ground_speed" (readsb) or "gs" (dump1090-fa)
        float gs = 0.0f;
        if (!ac["ground_speed"].isNull()) {
            gs = ac["ground_speed"].as<float>();
        } else if (!ac["gs"].isNull()) {
            gs = ac["gs"].as<float>();
        }
        a.velocity = gs * 0.514444f;  // knots to m/s

        // Track / heading in degrees
        a.trueTrack = ac["track"].isNull() ? 0.0f : ac["track"].as<float>();

        // Vertical rate: readsb provides "vertical_rate" in m/s
        // dump1090-fa provides "vert_rate" in fpm → m/s
        if (!ac["vertical_rate"].isNull()) {
            a.verticalRate = ac["vertical_rate"].as<float>();
        } else if (!ac["vert_rate"].isNull()) {
            a.verticalRate = ac["vert_rate"].as<float>() * 0.508f;  // fpm to m/s
        } else {
            a.verticalRate = 0.0f;
        }

        // Geometric altitude: readsb "geo_altitude" in feet, dump1090-fa "altitude" in feet
        float geoAltFeet = 0.0f;
        if (!ac["geo_altitude"].isNull()) {
            geoAltFeet = ac["geo_altitude"].as<float>();
        } else if (!ac["altitude"].isNull()) {
            geoAltFeet = ac["altitude"].as<float>();
        }
        a.geoAltitude = geoAltFeet * 0.3048f;

        // Squawk: readsb returns as string, dump1090-fa returns as int
        if (!ac["squawk"].isNull()) {
            if (ac["squawk"].is<String>()) {
                a.squawk = ac["squawk"].as<String>();
            } else {
                a.squawk = String(ac["squawk"].as<unsigned int>(), HEX);
            }
        } else {
            a.squawk = "";
        }

        // SPI not available in readsb
        a.spi = false;

        // Position source — assume ADS-B (0)
        a.positionSource = 0;
        a.category = 0;

        return a;
    }

}
