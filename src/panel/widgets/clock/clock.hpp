#ifndef WIDGETS_CLOCK_HPP
#define WIDGETS_CLOCK_HPP

#include "../../widget.hpp"
#include "wf-popover.hpp"
#include "weather.hpp"
#include <gtkmm/calendar.h>
#include <gtkmm/label.h>
#include <gtkmm/image.h>
#include <gtkmm/entry.h>
#include <gtkmm/box.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/hvbox.h>

class WayfireClock : public WayfireWidget
{
    /* Panel widget contents: icon | temperature | time */
    Gtk::HBox m_box;
    Gtk::Image weather_icon;
    Gtk::Label temp_label;
    Gtk::Label label;

    std::unique_ptr<WayfireMenuButton> button;

    /* Popover: current location + "Select" toggle + (search + list) + calendar */
    Gtk::VBox popover_box;
    Gtk::Label current_label;
    Gtk::Button select_button;
    Gtk::Box selection_area;
    Gtk::Entry search_entry;
    Gtk::ScrolledWindow locations_scroll;
    Gtk::Box locations_list;
    Gtk::Calendar calendar;

    Weather weather;
    std::vector<WeatherLocationEntry> all_locations;

    sigc::connection timeout;
    sigc::connection weather_timeout;

    WfOption<std::string> format{"panel/clock_format"};
    WfOption<std::string> font{"panel/clock_font"};
    WfOption<std::string> location_opt{"panel/weather_location"};
    WfOption<std::string> temp_unit_opt{"panel/weather_temperature_unit"};
    WfOption<std::string> speed_unit_opt{"panel/weather_speed_unit"};
    WfOption<int> update_interval{"panel/weather_update_interval"};

    void set_font();
    void on_calendar_shown();
    bool update_label();

    void apply_units();
    void apply_location();
    void on_weather_updated();
    void on_weather_shown();
    void rebuild_location_list();
    void on_search_changed();
    void activate_location(const WeatherLocationEntry& entry);
    void update_current_label();
    void open_selector();

  public:
    void init(Gtk::HBox *container) override;
    ~WayfireClock();
};

#endif /* end of include guard: WIDGETS_CLOCK_HPP */
