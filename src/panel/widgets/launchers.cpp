#include "launchers.hpp"
#include <giomm/file.h>
#include <glibmm/spawn.h>
#include <glibmm/keyfile.h>
#include <gtkmm/icontheme.h>
#include <gdk/gdkcairo.h>
#include <glibmm/main.h>
#include <cassert>
#include <iostream>
#include <fstream>
#include <set>
#include <algorithm>
#include <gtk-utils.hpp>
#include <wf-shell-app.hpp>

bool WfLauncherButton::initialize(std::string name, std::string icon, std::string label)
{
    app_info = Gio::DesktopAppInfo::create(name);

    if (icon == "none")
    {
        if (!app_info)
        {
            // attempt to interpret it as a .desktop file
            app_info = Gio::DesktopAppInfo::create_from_filename(name);
        }
    } else
    {
        // Generate a .desktop file in memory
        auto keyfile = Glib::KeyFile();
        keyfile.set_string("Desktop Entry", "Type", "Application");
        keyfile.set_string("Desktop Entry", "Exec", "/bin/sh -c \"" + name + "\"");
        keyfile.set_string("Desktop Entry", "Icon", icon);
        if (label == "")
        {
            label = name;
        }

        keyfile.set_string("Desktop Entry", "Name", label);

        // Hand off to have a custom launcher
        app_info = Gio::DesktopAppInfo::create_from_keyfile(keyfile);
    }

    /* Failed to get a launcher */
    if (!app_info)
    {
        return false;
    }

    button.set_image(m_icon);
    auto style = button.get_style_context();
    style->add_class("flat");
    style->add_class("launcher");

    button.signal_clicked().connect([=] () { launch(); });
    button.property_scale_factor().signal_changed()
        .connect([=] () {update_icon(); });
    icon_size.set_callback([=] () { update_icon(); });

    update_icon();

    button.set_tooltip_text(app_info->get_name());

    /* Right click opens a menu with a single "Remove" entry, which removes
     * this launcher from the panel and adjusts the config file. */
    remove_item.set_label("Remove");
    remove_item.signal_activate().connect([this] ()
    {
        if (on_remove)
        {
            on_remove(this);
        }
    });
    menu.append(remove_item);
    menu.attach_to_widget(button);
    menu.show_all();

    button.signal_button_press_event().connect([this] (GdkEventButton *ev) -> bool
    {
        if (ev->button == GDK_BUTTON_SECONDARY)
        {
            menu.popup_at_widget(&button, Gdk::GRAVITY_NORTH_WEST,
                Gdk::GRAVITY_SOUTH_WEST, NULL);
            return true;
        }

        return false;
    });

    return true;
}

void WfLauncherButton::update_icon()
{
    set_image_icon(m_icon, app_info->get_icon()->to_string(), icon_size);
}

void WfLauncherButton::launch()
{
    if (app_info)
    {
        app_info->launch(std::vector<Glib::RefPtr<Gio::File>>());
    }
}

WfLauncherButton::WfLauncherButton()
{}
WfLauncherButton::~WfLauncherButton()
{}

static bool begins_with(const std::string& string, const std::string& prefix)
{
    return string.size() >= prefix.size() &&
           string.substr(0, prefix.size()) == prefix;
}

static std::string trim_copy(const std::string& str)
{
    auto begin = str.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }

    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(begin, end - begin + 1);
}

/* Remove the given keys from the given section of the config file, keeping
 * all other lines (including comments) untouched. */
static void remove_keys_from_config_file(const std::string& path,
    const std::string& section_name, const std::set<std::string>& keys)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        return;
    }

    std::string line, current_section;
    std::vector<std::string> out;
    while (std::getline(in, line))
    {
        auto trimmed = trim_copy(line);

        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']')
        {
            current_section = trim_copy(trimmed.substr(1, trimmed.size() - 2));
            out.push_back(line);
            continue;
        }

        if (current_section == section_name && !trimmed.empty() &&
            trimmed.front() != '#' && trimmed.find('=') != std::string::npos)
        {
            auto key = trim_copy(trimmed.substr(0, trimmed.find('=')));
            if (keys.count(key))
            {
                continue;
            }
        }

        out.push_back(line);
    }

    in.close();

    std::ofstream out_file(path, std::ios::trunc);
    if (!out_file.is_open())
    {
        return;
    }

    for (auto& l : out)
    {
        out_file << l << "\n";
    }
}

launcher_container WayfireLaunchers::get_launchers_from_config()
{
    auto section = WayfireShellApp::get().config.get_section("panel");
    const std::string desktop_prefix    = "launcher_";
    const std::string file_icon_prefix  = "launcher_icon_";
    const std::string file_cmd_prefix   = "launcher_cmd_";
    const std::string file_label_prefix = "launcher_label_";

    launcher_container launchers;
    auto try_push_launcher = [&] (
        const std::string cmd, const std::string icon,
        const std::vector<std::string>& keys, const std::string label = "")
    {
        auto launcher = new WfLauncherButton();
        if (launcher->initialize(cmd, icon, label))
        {
            launcher->config_keys = keys;
            launcher->on_remove = [this] (WfLauncherButton *l)
            {
                remove_launcher(l);
            };
            launchers.push_back(std::unique_ptr<WfLauncherButton>(launcher));
        } else
        {
            delete launcher;
        }
    };

    for (auto opt : section->get_registered_options())
    {
        /* we have a command */
        if (begins_with(opt->get_name(), file_cmd_prefix))
        {
            /* extract launcher name, i.e the string after the prefix */
            auto launcher_name = opt->get_name().substr(file_cmd_prefix.size());
            /* look for the corresponding icon */
            auto icon_option = section->get_option_or(file_icon_prefix + launcher_name);
            if (icon_option)
            {
                auto keys = std::vector<std::string>{
                    opt->get_name(), file_icon_prefix + launcher_name};
                /* bingo, found command + icon
                 * now look for the corresponding label  */
                auto label_option = section->get_option_or(file_label_prefix + launcher_name);
                if (label_option)
                {
                    /* found label */
                    keys.push_back(file_label_prefix + launcher_name);
                    try_push_launcher(opt->get_value_str(), icon_option->get_value_str(),
                        keys, label_option->get_value_str());
                } else
                {
                    try_push_launcher(opt->get_value_str(), icon_option->get_value_str(), keys);
                }
            }
        }

        /* an entry is a deskop-file entry if the it has the desktop prefix
         * but not the file_icon, file_cmd or file_label prefix */
        if (begins_with(opt->get_name(), desktop_prefix) &&
            !begins_with(opt->get_name(), file_icon_prefix) &&
            !begins_with(opt->get_name(), file_cmd_prefix) &&
            !begins_with(opt->get_name(), file_label_prefix))
        {
            try_push_launcher(opt->get_value_str(), "none", {opt->get_name()});
        }
    }

    return launchers;
}

void WayfireLaunchers::remove_launcher(WfLauncherButton *launcher)
{
    /* Defer the actual removal so that the launcher (and the menu it belongs
     * to) is not destroyed while handling its own activate signal. */
    Glib::signal_timeout().connect([this, launcher] ()
    {
        auto it = std::find_if(launchers.begin(), launchers.end(),
            [launcher] (const auto& ptr) { return ptr.get() == launcher; });
        if (it == launchers.end())
        {
            return false;
        }

        auto keys = (*it)->config_keys;
        auto section = WayfireShellApp::get().config.get_section("panel");
        for (const auto& key : keys)
        {
            auto opt = section->get_option_or(key);
            if (opt)
            {
                section->unregister_option(opt);
            }
        }

        box.remove((*it)->button);
        launchers.erase(it);

        remove_keys_from_config_file(WayfireShellApp::get().get_config_file(),
            "panel", std::set<std::string>(keys.begin(), keys.end()));
        return false;
    }, 0);
}

void WayfireLaunchers::init(Gtk::HBox *container)
{
    box.get_style_context()->add_class("launchers");
    container->pack_start(box, false, false);
    handle_config_reload();
}

void WayfireLaunchers::handle_config_reload()
{
    box.set_spacing(WfOption<int>{"panel/launchers_spacing"});

    launchers = get_launchers_from_config();
    for (auto& l : launchers)
    {
        box.pack_start(l->button, false, false);
    }

    box.show_all();
}
