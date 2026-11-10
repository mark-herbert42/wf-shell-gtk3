#include "weather.hpp"

#include <glibmm.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <sstream>

static std::string rad2dms(double lat, double lon)
{
    char h  = lat > 0 ? 'N' : 'S';
    double d  = std::fabs(lat);
    int deg  = (int)std::floor(d);
    int min  = (int)std::floor(60 * (d - deg));

    char h2 = lon > 0 ? 'E' : 'W';
    double d2 = std::fabs(lon);
    int deg2 = (int)std::floor(d2);
    int min2 = (int)std::floor(60 * (d2 - deg2));

    char buf[64];
    snprintf(buf, sizeof(buf), "%02d-%02d%c %02d-%02d%c", deg, min, h, deg2, min2, h2);
    return buf;
}

Weather::Weather()
{
    prefs.type             = FORECAST_STATE;
    prefs.radar            = FALSE;
    prefs.radar_custom_url = NULL;
    prefs.temperature_unit = TEMP_UNIT_CENTIGRADE;
    prefs.speed_unit       = SPEED_UNIT_MS;
    prefs.pressure_unit    = PRESSURE_UNIT_MB;
    prefs.distance_unit    = DISTANCE_UNIT_KM;
}

Weather::~Weather()
{
    if (info)
    {
        weather_info_abort(info);
        weather_info_free(info);
        info = nullptr;
    }
}

sigc::signal<void()> Weather::updated()
{
    return m_updated;
}

bool Weather::is_valid() const
{
    return m_valid;
}

void Weather::set_units(TempUnit temp, SpeedUnit speed)
{
    prefs.temperature_unit = temp;
    prefs.speed_unit       = speed;
    if (info)
    {
        refresh();
    }
}

void Weather::set_location(const WeatherLocationEntry& loc)
{
    current = loc;
    build_info();
}

const WeatherLocationEntry& Weather::location() const
{
    return current;
}

void Weather::on_info_updated(WeatherInfo *info, gpointer data)
{
    auto self = (Weather*)data;
    self->m_valid = info && weather_info_is_valid(info);
    self->m_updated.emit();
}

void Weather::build_info()
{
    if (info)
    {
        weather_info_abort(info);
        weather_info_free(info);
        info = nullptr;
    }

    m_valid = false;

    if (current.code.empty())
    {
        m_updated.emit();
        return;
    }

    auto dms = rad2dms(current.lat, current.lon);
    auto wl = weather_location_new(current.name.c_str(), current.code.c_str(),
        NULL, NULL, dms.c_str(), NULL, NULL);

    info = weather_info_new(wl, &prefs, on_info_updated, this);
    weather_location_free(wl);
}

void Weather::refresh()
{
    if (info)
    {
        weather_info_abort(info);
        weather_info_update(info, &prefs, on_info_updated, this);
    } else
    {
        build_info();
    }
}

std::string Weather::icon_name() const
{
    if (!m_valid)
    {
        return "";
    }

    auto name = weather_info_get_icon_name(info);
    return name ? name : "";
}

std::string Weather::temperature() const
{
    if (!m_valid)
    {
        return "";
    }

    auto temp = weather_info_get_temp_summary(info);
    return temp ? temp : "";
}

std::string Weather::description() const
{
    if (!m_valid)
    {
        return "";
    }

    auto conditions = weather_info_get_conditions(info);
    auto sky = weather_info_get_sky(info);
    std::string result;
    if (conditions && strcmp(conditions, "-") != 0)
    {
        result += conditions;
        if (sky)
        {
            result += ", ";
            result += sky;
        }
    } else if (sky)
    {
        result += sky;
    }

    return result;
}

std::string Weather::wind() const
{
    if (!m_valid)
    {
        return "";
    }

    auto wind = weather_info_get_wind(info);
    return wind ? wind : "";
}

static std::string format_time(time_t t)
{
    struct tm tm_out;
    if (!localtime_r(&t, &tm_out))
    {
        return "???";
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M", &tm_out);
    return buf;
}

std::string Weather::sun() const
{
    if (!m_valid)
    {
        return "";
    }

    time_t rise = 0, set = 0;
    auto rise_str = weather_info_get_value_sunrise(info, &rise) ? format_time(rise) : std::string("???");
    auto set_str  = weather_info_get_value_sunset(info, &set) ? format_time(set) : std::string("???");
    return "Sunrise: " + rise_str + " / Sunset: " + set_str;
}

std::string Weather::tooltip_markup() const
{
    if (!m_valid)
    {
        return "";
    }

    auto desc = description();
    auto temp = temperature();
    auto wind_str = wind();
    auto sun_str = sun();

    std::string tip = "<b>" + desc + "</b>\n" + temp;
    if (!wind_str.empty())
    {
        tip += "\n" + wind_str;
    }

    tip += "\n" + sun_str;
    return tip;
}

/* ------------------------------------------------------------------------ */

static void collect_locations(MateWeatherLocation *loc,
    std::vector<WeatherLocationEntry>& out)
{
    if (!loc)
    {
        return;
    }

    auto code = mateweather_location_get_code(loc);
    if (code && mateweather_location_has_coords(loc))
    {
        double lat = 0, lon = 0;
        mateweather_location_get_coords(loc, &lat, &lon);

        auto city = mateweather_location_get_city_name(loc);
        auto disp = city ? city : mateweather_location_get_name(loc);

        auto zone = mateweather_location_get_timezone(loc);
        std::string tz = zone ? mateweather_timezone_get_tzid(zone) : "";

        bool exists = false;
        for (auto& e : out)
        {
            if (e.code == code)
            {
                exists = true;
                break;
            }
        }

        if (!exists && disp)
        {
            out.push_back({disp, code, tz, lat, lon});
        }

        g_free(city);
    }

    MateWeatherLocation **children = mateweather_location_get_children(loc);
    for (int i = 0; children && children[i]; i++)
    {
        collect_locations(children[i], out);
    }

    mateweather_location_free_children(loc, children);
}

std::vector<WeatherLocationEntry> Weather::list_locations()
{
    /* The world database is static data, and libmateweather is not safe to
     * re-parse via mateweather_location_new_world() more than once in a single
     * process (a second call aborts inside libxml2, which crashed the panel
     * when a second monitor created another clock widget). Parse it once and
     * cache the result. */
    static std::vector<WeatherLocationEntry> cache;
    static bool loaded = false;

    if (!loaded)
    {
        auto world = mateweather_location_new_world(FALSE);
        if (world)
        {
            collect_locations(world, cache);
            mateweather_location_unref(world);
        }

        loaded = true;
    }

    return cache;
}

std::string Weather::encode(const WeatherLocationEntry& loc)
{
    std::ostringstream ss;
    ss << loc.name << ";" << loc.code << ";" << loc.tz << ";"
       << loc.lat << ";" << loc.lon;
    return ss.str();
}

WeatherLocationEntry Weather::decode(const std::string& value)
{
    WeatherLocationEntry entry;
    if (value.empty())
    {
        return entry;
    }

    size_t p1 = value.find(';');
    size_t p2 = (p1 == std::string::npos) ? std::string::npos : value.find(';', p1 + 1);
    size_t p3 = (p2 == std::string::npos) ? std::string::npos : value.find(';', p2 + 1);
    size_t p4 = (p3 == std::string::npos) ? std::string::npos : value.find(';', p3 + 1);
    if (p4 == std::string::npos)
    {
        return entry;
    }

    entry.name = value.substr(0, p1);
    entry.code = value.substr(p1 + 1, p2 - p1 - 1);
    entry.tz   = value.substr(p2 + 1, p3 - p2 - 1);
    entry.lat  = g_ascii_strtod(value.substr(p3 + 1, p4 - p3 - 1).c_str(), NULL);
    entry.lon  = g_ascii_strtod(value.substr(p4 + 1).c_str(), NULL);
    return entry;
}

std::string Weather::timezone() const
{
    return current.tz;
}
