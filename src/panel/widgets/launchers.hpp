#ifndef LAUNCHERS_HPP
#define LAUNCHERS_HPP

#include "../widget.hpp"
#include <vector>
#include <string>
#include <functional>
#include <giomm/desktopappinfo.h>
#include <gtkmm/image.h>
#include <gtkmm/hvbox.h>
#include <gtkmm/button.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <wayfire/util/duration.hpp>

struct WfLauncherButton
{
    Gtk::Image m_icon;
    Gtk::Button button;
    Gtk::Menu menu;
    Gtk::MenuItem remove_item;
    Glib::RefPtr<Gio::DesktopAppInfo> app_info;
    WfOption<int> icon_size{"panel/launchers_size"};

    /* Names of the [panel] options this launcher corresponds to in the
     * config file. They are removed when the launcher is removed. */
    std::vector<std::string> config_keys;

    /* Invoked when the "Remove" menu entry is activated */
    std::function<void(WfLauncherButton*)> on_remove;

    WfLauncherButton();
    WfLauncherButton(const WfLauncherButton& other) = delete;
    WfLauncherButton& operator =(const WfLauncherButton&) = delete;
    ~WfLauncherButton();

    bool initialize(std::string name, std::string icon = "none", std::string label = "");
    void launch();
    void update_icon();
};

using launcher_container = std::vector<std::unique_ptr<WfLauncherButton>>;
class WayfireLaunchers : public WayfireWidget
{
    Gtk::HBox box;
    launcher_container launchers;
    launcher_container get_launchers_from_config();
    void remove_launcher(WfLauncherButton *launcher);

  public:
    virtual void init(Gtk::HBox *container);
    virtual void handle_config_reload();
    virtual ~WayfireLaunchers()
    {}
};


#endif /* end of include guard: LAUNCHERS_HPP */
