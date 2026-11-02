#include <glibmm.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <gtkmm/button.h>
#include <gtkmm/tooltip.h>
#include <gtk-utils.hpp>
#include <wf-shell-app.hpp>
#include "clock.hpp"

static std::string clock_trim(const std::string& str)
{
    auto begin = str.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }

    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(begin, end - begin + 1);
}

/* Set "key = value" inside the [panel] section of the config file, updating an
 * existing entry or inserting a new one, keeping comments/other lines intact. */
static void set_panel_option_in_file(const std::string& path,
    const std::string& key, const std::string& value)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (in.is_open())
    {
        std::string line;
        while (std::getline(in, line))
        {
            lines.push_back(line);
        }

        in.close();
    }

    const std::string new_line = key + " = " + value;
    bool replaced = false;
    bool in_panel = false;
    bool has_panel = false;
    for (size_t i = 0; i < lines.size(); i++)
    {
        auto trimmed = clock_trim(lines[i]);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']')
        {
            in_panel = (trimmed == "[panel]");
            has_panel = has_panel || in_panel;
            continue;
        }

        if (in_panel && !trimmed.empty() && trimmed.front() != '#' &&
            trimmed.find('=') != std::string::npos)
        {
            if (clock_trim(trimmed.substr(0, trimmed.find('='))) == key)
            {
                lines[i] = new_line;
                replaced = true;
            }
        }
    }

    if (!replaced)
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (clock_trim(lines[i]) == "[panel]")
            {
                lines.insert(lines.begin() + i + 1, new_line);
                replaced = true;
                break;
            }
        }
    }

    if (!replaced && !has_panel)
    {
        if (!lines.empty())
        {
            lines.push_back("");
        }

        lines.push_back("[panel]");
        lines.push_back(new_line);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
    {
        return;
    }

    for (auto& l : lines)
    {
        out << l << "\n";
    }
}

static TempUnit parse_temp_unit(const std::string& unit)
{
    if (unit == "kelvin")      return TEMP_UNIT_KELVIN;
    if (unit == "fahrenheit")  return TEMP_UNIT_FAHRENHEIT;
    return TEMP_UNIT_CENTIGRADE;
}

static SpeedUnit parse_speed_unit(const std::string& unit)
{
    if (unit == "kph")    return SPEED_UNIT_KPH;
    if (unit == "mph")    return SPEED_UNIT_MPH;
    if (unit == "knots")  return SPEED_UNIT_KNOTS;
    if (unit == "bft")    return SPEED_UNIT_BFT;
    return SPEED_UNIT_MS;
}

void WayfireClock::init(Gtk::HBox *container)
{
    button = std::make_unique<WayfireMenuButton>("panel");
    button->get_style_context()->add_class("clock");

    m_box.set_spacing(6);
    m_box.pack_start(weather_icon, false, false);
    m_box.pack_start(temp_label, false, false);
    m_box.pack_start(label, false, false);
    button->add(m_box);

    update_label();
    set_font();
    font.set_callback([=] () { set_font(); });

    timeout = Glib::signal_timeout().connect_seconds(
        [=] () { return update_label(); }, 1);

    /* Weather tooltip on the panel widget */
    button->set_has_tooltip();
    button->signal_query_tooltip().connect(
        [=] (int, int, bool, const Glib::RefPtr<Gtk::Tooltip>& tooltip) -> bool
    {
        auto tip = weather.tooltip_markup();
        if (tip.empty())
        {
            return false;
        }

        tooltip->set_markup(tip);
        return true;
    });

    /* Weather engine */
    all_locations = Weather::list_locations();
    weather.updated().connect([=] () { on_weather_updated(); });
    apply_units();
    temp_unit_opt.set_callback([=] () { apply_units(); });
    speed_unit_opt.set_callback([=] () { apply_units(); });
    location_opt.set_callback([=] () { apply_location(); });
    apply_location();

    weather_timeout = Glib::signal_timeout().connect_seconds(
        [=] () { weather.refresh(); return true; },
        std::max(1, (int)update_interval));

    /* Popover layout:
     *   line 1: current (active) location
     *   line 2: "Select location" toggle + collapsible search/list area
     *   below : calendar                                              */
    current_label.set_xalign(0.5);
    current_label.get_style_context()->add_class("clock-current-location");
    update_current_label();

    select_button.set_label("Select location");
    select_button.get_style_context()->add_class("flat");
    select_button.signal_clicked().connect([=] () { open_selector(); });

    search_entry.set_placeholder_text("Search location...");
    search_entry.signal_changed().connect([=] () { on_search_changed(); });

    locations_list.set_orientation(Gtk::ORIENTATION_VERTICAL);
    locations_list.set_vexpand(true);
    locations_scroll.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    locations_scroll.add(locations_list);
    locations_scroll.set_size_request(-1, 200);

    selection_area.set_orientation(Gtk::ORIENTATION_VERTICAL);
    selection_area.pack_start(search_entry, false, false);
    selection_area.pack_start(locations_scroll, true, true);

    calendar.set_valign(Gtk::ALIGN_START);

    popover_box.pack_start(current_label, false, false);
    popover_box.pack_start(select_button, false, false);
    popover_box.pack_start(selection_area, true, true);
    popover_box.pack_start(calendar, false, false);

    button->get_popover()->get_style_context()->add_class("clock-popover");
    button->get_popover()->add(popover_box);
    button->get_popover()->signal_show().connect_notify(
        [=] ()
    {
        update_current_label();
        selection_area.hide();
        on_calendar_shown();
        on_weather_shown();
    });

    /* Populate the location list up-front so the dialog is usable even when
     * no location has been selected yet */
    rebuild_location_list();

    container->pack_start(*button, false, false);
    button->show_all();
    popover_box.show_all();
}

void WayfireClock::apply_units()
{
    weather.set_units(parse_temp_unit((std::string)temp_unit_opt),
        parse_speed_unit((std::string)speed_unit_opt));
}

void WayfireClock::apply_location()
{
    weather.set_location(Weather::decode((std::string)location_opt));
}

void WayfireClock::on_weather_updated()
{
    auto icon = weather.icon_name();
    if (!icon.empty())
    {
        weather_icon.set_from_icon_name(icon, Gtk::ICON_SIZE_LARGE_TOOLBAR);
        weather_icon.set_pixel_size(24);
        weather_icon.show();
    } else
    {
        weather_icon.hide();
    }

    auto temp = weather.temperature();
    temp_label.set_text(temp);
    temp_label.set_visible(!temp.empty());
}

void WayfireClock::on_weather_shown()
{
    /* Refresh the network data each time the popover is opened */
    weather.refresh();
}

void WayfireClock::on_calendar_shown()
{
    auto now = Glib::DateTime::create_now_local();
    calendar.select_month(now.get_month() - 1, now.get_year());
    calendar.select_day(now.get_day_of_month());
}

void WayfireClock::on_search_changed()
{
    rebuild_location_list();
}

void WayfireClock::rebuild_location_list()
{
    locations_list.foreach([=] (Gtk::Widget& w) { locations_list.remove(w); });

    auto query = search_entry.get_text();
    query = query.lowercase();
    const size_t limit = 100;
    size_t shown = 0;

    for (auto& entry : all_locations)
    {
        if (shown >= limit)
        {
            break;
        }

        if (!query.empty())
        {
            auto hay = Glib::ustring(entry.name).lowercase();
            if (hay.find(query) == Glib::ustring::npos)
            {
                continue;
            }
        }

        auto row = new Gtk::HBox();
        auto lbl = new Gtk::Label(entry.name + " (" + entry.code + ")");
        lbl->set_xalign(0.0);
        auto set_btn = new Gtk::Button("Set");
        set_btn->get_style_context()->add_class("flat");
        auto stored = entry;
        set_btn->signal_clicked().connect([=] () { activate_location(stored); });

        row->pack_start(*lbl, true, true);
        row->pack_start(*set_btn, false, false);
        locations_list.add(*row);
        shown++;
    }

    locations_list.show_all();
}

void WayfireClock::activate_location(const WeatherLocationEntry& entry)
{
    auto encoded = Weather::encode(entry);
    set_panel_option_in_file(WayfireShellApp::get().get_config_file(),
        "weather_location", encoded);

    /* Apply immediately; the config reload re-applies the same value */
    weather.set_location(entry);
    update_current_label();
    selection_area.hide();
    button->get_popover()->popdown();
}

void WayfireClock::update_current_label()
{
    auto loc = weather.location();
    if (loc.name.empty() || loc.code.empty())
    {
        current_label.set_markup("<b>No location selected</b>");
    } else
    {
        current_label.set_markup("<b>" + Glib::Markup::escape_text(loc.name) +
            " (" + Glib::Markup::escape_text(loc.code) + ")</b>");
    }
}

void WayfireClock::open_selector()
{
    selection_area.show_all();
    search_entry.grab_focus();
}

bool WayfireClock::update_label()
{
    Glib::DateTime time = Glib::DateTime::create_now_local();

    /* Respect the timezone of the selected weather location, if any */
    auto tz = weather.timezone();
    if (!tz.empty())
    {
        try
        {
            auto zone = Glib::TimeZone::create(tz);
            time = Glib::DateTime::create_now(zone);
        } catch (const Glib::Error& )
        {
            time = Glib::DateTime::create_now_local();
        } catch (const std::exception& )
        {
            time = Glib::DateTime::create_now_local();
        }
    }

    auto text = time.format((std::string)format);

    int i = 0;
    while (i < (int)text.length() && text[i] == ' ')
    {
        i++;
    }

    label.set_text(text.substr(i));
    return true;
}

void WayfireClock::set_font()
{
    if ((std::string)font == "default")
    {
        label.unset_font();
    } else
    {
        label.override_font(Pango::FontDescription((std::string)font));
    }
}

WayfireClock::~WayfireClock()
{
    timeout.disconnect();
    weather_timeout.disconnect();
}
