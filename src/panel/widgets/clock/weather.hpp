#ifndef WIDGETS_CLOCK_WEATHER_HPP
#define WIDGETS_CLOCK_WEATHER_HPP

#define MATEWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libmateweather/weather.h>
#include <libmateweather/mateweather-location.h>

#include <sigc++/sigc++.h>
#include <string>
#include <vector>

/* A single selectable weather location (a weather station entry). */
struct WeatherLocationEntry
{
    std::string name;   /* display name (city) */
    std::string code;   /* weather/radar station code */
    std::string tz;     /* IANA timezone id (e.g. "Europe/Berlin") */
    double lat = 0.0;
    double lon = 0.0;
};

/*
 * Thin C++ wrapper around libmateweather's asynchronous WeatherInfo.
 *
 * Weather data is fetched over the network; whenever a fetch completes, the
 * updated() signal is emitted so that the UI can refresh itself.
 */
class Weather
{
    WeatherInfo *info = nullptr;
    WeatherPrefs prefs{};
    WeatherLocationEntry current;
    bool m_valid = false;

    sigc::signal<void()> m_updated;

    static void on_info_updated(WeatherInfo *info, gpointer data);
    void build_info();

  public:
    Weather();
    ~Weather();

    sigc::signal<void()> updated();
    bool is_valid() const;

    void set_units(TempUnit temp, SpeedUnit speed);
    void set_location(const WeatherLocationEntry& loc);
    const WeatherLocationEntry& location() const;
    std::string timezone() const;
    void refresh();

    /* String accessors (empty when the data is not valid) */
    std::string icon_name() const;
    std::string temperature() const;
    std::string description() const;   /* conditions + sky */
    std::string wind() const;
    std::string sun() const;           /* sunrise / sunset */
    std::string tooltip_markup() const;

    /* Enumerate all weather-station locations available in the database */
    static std::vector<WeatherLocationEntry> list_locations();

    /* Serialize/deserialize a location to the "Name;CODE;LAT;LON" format */
    static std::string encode(const WeatherLocationEntry& loc);
    static WeatherLocationEntry decode(const std::string& value);
};

#endif /* end of include guard: WIDGETS_CLOCK_WEATHER_HPP */
