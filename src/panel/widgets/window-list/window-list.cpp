#include <iostream>
#include <algorithm>
#include <glibmm.h>
#include <gdk/gdkwayland.h>

#include "toplevel.hpp"
#include "window-list.hpp"
#include "panel.hpp"

WayfireWindowListBox::WayfireWindowListBox() : Gtk::HBox()
{}

void WayfireWindowListBox::set_top_widget(Gtk::Widget *top)
{
    this->top_widget = top;

    if (top_widget)
    {
        /* Set original top_x to where the widget currently is, so that we don't
         * mess with it before the real position is set */
        this->top_x = get_absolute_position(0, *top);
    }

    set_top_x(top_x);
}

void WayfireWindowListBox::set_top_x(int x)
{
    /* Make sure that the widget doesn't go outside of the box */
    if (this->top_widget)
    {
        x = std::min(x, get_allocated_width() - top_widget->get_allocated_width());
    }

    if (this->top_widget)
    {
        x = std::max(x, 0);
    }

    this->top_x = x;

    queue_allocate();
    queue_draw();

    auto alloc = this->get_allocation();
    on_size_allocate(alloc);
}

static void for_each_child_callback(GtkWidget *widget, gpointer data)
{
    auto v = (std::vector<GtkWidget*>*)data;
    v->push_back(widget);
}

std::vector<Gtk::Widget*> WayfireWindowListBox::get_unsorted_widgets()
{
    std::vector<GtkWidget*> children;
    HBox::forall_vfunc(true, &for_each_child_callback, &children);

    std::vector<Gtk::Widget*> result;
    for (auto& child : children)
    {
        result.push_back(Glib::wrap(child));
    }

    return result;
}

void WayfireWindowListBox::forall_vfunc(gboolean value, GtkCallback callback, gpointer callback_data)
{
    std::vector<GtkWidget*> children;
    HBox::forall_vfunc(true, &for_each_child_callback, &children);

    if (top_widget)
    {
        auto it = std::find(children.begin(), children.end(), top_widget->gobj());
        children.erase(it);
        children.push_back(top_widget->gobj());
    }

    for (auto& child : children)
    {
        callback(child, callback_data);
    }
}

void WayfireWindowListBox::on_size_allocate(Gtk::Allocation& alloc)
{
    HBox::on_size_allocate(alloc);

    if (top_widget)
    {
        auto alloc = top_widget->get_allocation();
        alloc.set_x(this->top_x);
        top_widget->size_allocate(alloc);
    }
}

int WayfireWindowListBox::get_absolute_position(int x, Gtk::Widget& ref)
{
    auto w = &ref;
    while (w && w != this)
    {
        auto allocation = w->get_allocation();
        x += allocation.get_x();
        w  = w->get_parent();
    }

    return x;
}

Gtk::Widget*WayfireWindowListBox::get_widget_at(int x)
{
    Gtk::Allocation given_point{x, get_allocated_height() / 2, 1, 1};

    /* Widgets are stored bottom to top, so we will return the bottom-most
     * widget at the given position */
    auto children = this->get_children();
    for (auto& child : children)
    {
        if (child->get_allocation().intersects(given_point))
        {
            return child;
        }
    }

    return nullptr;
}

#define DEFAULT_SIZE_PC 0.1

static void handle_manager_toplevel(void *data, zwlr_foreign_toplevel_manager_v1 *manager,
    zwlr_foreign_toplevel_handle_v1 *toplevel)
{
    WayfireWindowList *window_list = (WayfireWindowList*)data;
    window_list->handle_new_toplevel(toplevel);
}

static void handle_manager_finished(void *data, zwlr_foreign_toplevel_manager_v1 *manager)
{}

zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_v1_impl = {
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

/* -------------------------------------------------------------------------- */
/*  ext-workspace tracking (to know the active workspace)                      */
/* -------------------------------------------------------------------------- */

/* Maps every ext_workspace_handle_v1 client object (from any binding) to the
 * compositor-assigned workspace name. Used to compare workspaces across the
 * different ext_workspace_manager bindings that exist when several monitors are
 * connected (one window-list, hence one binding, per output). */
static std::map<ext_workspace_handle_v1*, std::string> ws_names;

static void handle_extws_handle_state(void *data, ext_workspace_handle_v1 *ws, uint32_t state)
{
    static_cast<WayfireWindowList*>(data)->handle_workspace_state(ws, state);
}

static void handle_extws_handle_removed(void *data, ext_workspace_handle_v1 *ws)
{
    static_cast<WayfireWindowList*>(data)->handle_workspace_removed(ws);
}

static struct ext_workspace_handle_v1_listener extws_handle_impl = {
    .id           = [] (void*, ext_workspace_handle_v1*, const char*) {},
    .name         = [] (void *data, ext_workspace_handle_v1 *ws, const char *name)
    {
        ws_names[ws] = name ? name : "";
        static_cast<WayfireWindowList*>(data)->recompute_current_workspace();
    },
    .coordinates  = [] (void*, ext_workspace_handle_v1*, wl_array*) {},
    .state        = handle_extws_handle_state,
    .capabilities = [] (void*, ext_workspace_handle_v1*, uint32_t) {},
    .removed      = handle_extws_handle_removed,
};

static const struct ext_workspace_group_handle_v1_listener extws_group_impl = {
    .capabilities    = [] (void*, ext_workspace_group_handle_v1*, uint32_t) {},
    .output_enter    = [] (void *data, ext_workspace_group_handle_v1 *group,
                           wl_output *output)
    {
        static_cast<WayfireWindowList*>(data)->handle_group_output_enter(group, output);
    },
    .output_leave    = [] (void *data, ext_workspace_group_handle_v1 *group,
                           wl_output *output)
    {
        static_cast<WayfireWindowList*>(data)->handle_group_output_leave(group, output);
    },
    .workspace_enter = [] (void *data, ext_workspace_group_handle_v1 *group,
                           ext_workspace_handle_v1 *ws)
    {
        static_cast<WayfireWindowList*>(data)->handle_group_workspace_enter(group, ws);
    },
    .workspace_leave = [] (void *data, ext_workspace_group_handle_v1 *group,
                           ext_workspace_handle_v1 *ws)
    {
        static_cast<WayfireWindowList*>(data)->handle_group_workspace_leave(group, ws);
    },
    .removed         = [] (void *data, ext_workspace_group_handle_v1 *group)
    {
        static_cast<WayfireWindowList*>(data)->handle_group_removed(group);
    },
};

static void handle_extws_workspace_group(void *data, ext_workspace_manager_v1*,
    ext_workspace_group_handle_v1 *group)
{
    ext_workspace_group_handle_v1_add_listener(group, &extws_group_impl, data);
    static_cast<WayfireWindowList*>(data)->handle_workspace_group(group);
}

static void handle_extws_workspace(void *data, ext_workspace_manager_v1*,
    ext_workspace_handle_v1 *ws)
{
    static_cast<WayfireWindowList*>(data)->handle_workspace_created(ws);
}

static void handle_extws_done(void *data, ext_workspace_manager_v1*)
{
    static_cast<WayfireWindowList*>(data)->handle_workspace_done();
}

static void handle_extws_finished(void*, ext_workspace_manager_v1*)
{}

static struct ext_workspace_manager_v1_listener extws_manager_impl = {
    .workspace_group = handle_extws_workspace_group,
    .workspace       = handle_extws_workspace,
    .done            = handle_extws_done,
    .finished        = handle_extws_finished,
};

static void registry_add_object(void *data, wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version)
{
    WayfireWindowList *window_list = (WayfireWindowList*)data;
    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
    {
        auto zwlr_toplevel_manager = (zwlr_foreign_toplevel_manager_v1*)
            wl_registry_bind(registry, name,
            &zwlr_foreign_toplevel_manager_v1_interface,
            std::min(version, 3u));

        window_list->handle_toplevel_manager(zwlr_toplevel_manager);
    } else if (strcmp(interface, ext_workspace_manager_v1_interface.name) == 0)
    {
        auto *manager = (ext_workspace_manager_v1*)wl_registry_bind(registry, name,
            &ext_workspace_manager_v1_interface, std::min(version, 1u));
        window_list->handle_ws_manager(manager);
    } else if (strcmp(interface,
        xfce_foreign_toplevel_manager_private_v1_interface.name) == 0)
    {
        auto *manager = (xfce_foreign_toplevel_manager_private_v1*)
            wl_registry_bind(registry, name,
            &xfce_foreign_toplevel_manager_private_v1_interface,
            std::min(version, 1u));
        window_list->handle_xfce_manager(manager);
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name)
{}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};

void WayfireWindowList::init(Gtk::HBox *container)
{
    auto gdk_display = gdk_display_get_default();
    auto display     = gdk_wayland_display_get_wl_display(gdk_display);

    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display);

    if (!this->manager)
    {
        std::cerr << "Compositor doesn't support" <<
            " wlr-foreign-toplevel-management." <<
            "The window-list widget will not be initialized." << std::endl;
        wl_registry_destroy(registry);
        return;
    }

    scrolled_window.get_style_context()->add_class("window-list");

    wl_registry_destroy(registry);
    zwlr_foreign_toplevel_manager_v1_add_listener(manager,
        &toplevel_manager_v1_impl, this);

    scrolled_window.signal_draw().connect_notify(
        sigc::mem_fun(this, &WayfireWindowList::on_draw));

    box.set_homogeneous(false);
    scrolled_window.add(box);
    scrolled_window.set_propagate_natural_width(true);
    /* NEVER (instead of AUTOMATIC) makes the scrolled window shrink its
     * content to fit the available space instead of showing a scrollbar.
     * Combined with the ellipsized labels this compacts the buttons. */
    scrolled_window.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_NEVER);
    container->pack_start(scrolled_window, false, false);
    scrolled_window.show_all();
}

void WayfireWindowList::set_button_width(int width)
{
    for (auto& toplevel : toplevels)
    {
        if (toplevel.second)
        {
            toplevel.second->set_width(width);
        }
    }
}

int WayfireWindowList::get_default_button_width()
{
    /* A width of 0 means "natural size": the button grows just enough to fit
     * its icon plus the (already capped to max_chars) title, without any
     * leftover whitespace. */
    return 0;
}

int WayfireWindowList::get_target_button_width()
{
    int num_children = box.get_children().size();

    if (num_children > 0)
    {
        /* The list overflows the panel, so shrink every button to the space
         * we have for it (the title gets truncated to fit). */
        return std::max(1,
            scrolled_window.get_allocated_width() / num_children);
    }

    return get_default_button_width();
}

void WayfireWindowList::on_draw(const Cairo::RefPtr<Cairo::Context>& ctx)
{
    /* The scrolled window uses POLICY_NEVER with an ellipsized label, so the
     * box shrinks-to-fit the available space on its own. The widget is packed
     * non-expanding, so its width equals the content (icon + capped title) and
     * never grows beyond it; removing a button reflows the box to the smaller
     * content automatically. No manual width forcing is needed. */
}

void WayfireWindowList::add_output(WayfireOutput *output)
{
    std::unique_ptr<WayfireWindowList>();
}

void WayfireWindowList::handle_toplevel_manager(zwlr_foreign_toplevel_manager_v1 *manager)
{
    this->manager = manager;
}

void WayfireWindowList::handle_ws_manager(ext_workspace_manager_v1 *manager)
{
    this->ws_manager = manager;
    ext_workspace_manager_v1_add_listener(manager, &extws_manager_impl, this);
}

void WayfireWindowList::handle_xfce_manager(
    xfce_foreign_toplevel_manager_private_v1 *manager)
{
    this->xfce_manager = manager;
}

void WayfireWindowList::handle_workspace_created(ext_workspace_handle_v1 *ws)
{
    ws_handles.push_back(ws);
    ext_workspace_handle_v1_add_listener(ws, &extws_handle_impl, this);
}

void WayfireWindowList::handle_workspace_state(ext_workspace_handle_v1 *ws,
    uint32_t state)
{
    ws_states[ws] = state;
    recompute_current_workspace();
}

void WayfireWindowList::handle_workspace_removed(ext_workspace_handle_v1 *ws)
{
    ws_handles.erase(std::remove(ws_handles.begin(), ws_handles.end(), ws),
        ws_handles.end());
    ws_states.erase(ws);

    for (auto& entry : ws_group_workspaces)
    {
        auto& vec = entry.second;
        vec.erase(std::remove(vec.begin(), vec.end(), ws), vec.end());
    }

    if (current_workspace == ws)
    {
        current_workspace = nullptr;
    }

    ws_names.erase(ws);
    ext_workspace_handle_v1_destroy(ws);
    recompute_current_workspace();
    refresh_visibility();
}

void WayfireWindowList::handle_workspace_done()
{
    recompute_current_workspace();
    refresh_visibility();
}

void WayfireWindowList::handle_workspace_group(ext_workspace_group_handle_v1 *group)
{
    ws_groups.push_back(group);
}

void WayfireWindowList::handle_group_output_enter(ext_workspace_group_handle_v1 *group,
    wl_output *output)
{
    ws_group_output[group] = output;
    recompute_current_workspace();
}

void WayfireWindowList::handle_group_output_leave(ext_workspace_group_handle_v1 *group,
    wl_output *output)
{
    auto it = ws_group_output.find(group);
    if ((it != ws_group_output.end()) && (it->second == output))
    {
        ws_group_output.erase(it);
        recompute_current_workspace();
    }
}

void WayfireWindowList::handle_group_workspace_enter(ext_workspace_group_handle_v1 *group,
    ext_workspace_handle_v1 *ws)
{
    auto& vec = ws_group_workspaces[group];
    if (std::find(vec.begin(), vec.end(), ws) == vec.end())
    {
        vec.push_back(ws);
    }

    recompute_current_workspace();
}

void WayfireWindowList::handle_group_workspace_leave(ext_workspace_group_handle_v1 *group,
    ext_workspace_handle_v1 *ws)
{
    auto it = ws_group_workspaces.find(group);
    if (it != ws_group_workspaces.end())
    {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), ws), vec.end());
    }

    recompute_current_workspace();
}

void WayfireWindowList::handle_group_removed(ext_workspace_group_handle_v1 *group)
{
    ws_groups.erase(std::remove(ws_groups.begin(), ws_groups.end(), group),
        ws_groups.end());
    ws_group_output.erase(group);
    ws_group_workspaces.erase(group);
    recompute_current_workspace();
    ext_workspace_group_handle_v1_destroy(group);
}

/* The active workspace is tracked per output: each output has its own workspace
 * group, so we must only consider the workspaces of the group that contains
 * this window-list's output (otherwise a second monitor's active workspace
 * would win and no buttons would ever be added on the other monitor). */
void WayfireWindowList::recompute_current_workspace()
{
    ext_workspace_handle_v1 *active = nullptr;
    for (auto *group : ws_groups)
    {
        auto oit = ws_group_output.find(group);
        if ((oit == ws_group_output.end()) || (oit->second != output->wo))
        {
            continue;
        }

        auto wit = ws_group_workspaces.find(group);
        if (wit == ws_group_workspaces.end())
        {
            continue;
        }

        for (auto *ws : wit->second)
        {
            auto sit = ws_states.find(ws);
            if ((sit != ws_states.end()) &&
                (sit->second & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE))
            {
                active = ws;
                break;
            }
        }

        if (active)
        {
            break;
        }
    }

    current_workspace = active;
    current_workspace_name = active ? ws_names[active] : std::string();
}

bool WayfireWindowList::on_current_workspace(ext_workspace_handle_v1 *ws)
{
    /* Without the bridge protocols we cannot know the workspace, so we
     * never filter (and thus show everything). */
    if (!only_current_workspace || !xfce_manager || !ws_manager)
    {
        return true;
    }

    /* Not determined yet (e.g. this output's workspace group has not been
     * announced): don't filter, otherwise the list would stay empty. */
    if (current_workspace_name.empty())
    {
        return true;
    }

    if (!ws)
    {
        return false;
    }

    /* Compare by compositor-assigned name: the xfce bridge may pass an
     * ext_workspace_handle_v1 object that belongs to a different client binding
     * (another monitor's window-list), so pointer identity is unreliable. */
    auto it = ws_names.find(ws);
    return (it != ws_names.end()) && (it->second == current_workspace_name);
}

void WayfireWindowList::refresh_visibility()
{
    for (auto& toplevel : toplevels)
    {
        if (toplevel.second)
        {
            toplevel.second->update_visibility();
        }
    }
}

void WayfireWindowList::handle_new_toplevel(zwlr_foreign_toplevel_handle_v1 *handle)
{
    toplevels[handle] = std::unique_ptr<WayfireToplevel>(new WayfireToplevel(this, handle));
    /* The size will be updated in the next on_draw() if needed */
    toplevels[handle]->set_width(get_default_button_width());
}

void WayfireWindowList::handle_toplevel_closed(zwlr_foreign_toplevel_handle_v1 *handle)
{
    toplevels.erase(handle);

    /* Removing a child makes the box reflow to the smaller content and the
     * non-expanding widget shrink to it; the ellipsized labels compact when
     * the space is still full. No manual width recalculation is needed. */
}

WayfireWindowList::WayfireWindowList(WayfireOutput *output)
{
    this->output = output;
    only_current_workspace.set_callback([this] ()
    {
        refresh_visibility();
    });
}

WayfireWindowList::~WayfireWindowList()
{
    /* The ext-workspace handle/group listeners were registered with this as
     * their data. When an output is disconnected the compositor may still have
     * workspace events queued (e.g. a state change during teardown); if the
     * objects outlive this window-list those events call back into freed memory
     * (crash in ws_states[ws]). Destroying the client objects here releases
     * their ids, so any queued events are dropped instead of dispatched. */
    for (auto *ws : ws_handles)
    {
        ext_workspace_handle_v1_destroy(ws);
    }

    for (auto *group : ws_groups)
    {
        ext_workspace_group_handle_v1_destroy(group);
    }

    if (xfce_manager)
    {
        xfce_foreign_toplevel_manager_private_v1_destroy(xfce_manager);
    }

    if (ws_manager)
    {
        ext_workspace_manager_v1_destroy(ws_manager);
    }

    zwlr_foreign_toplevel_manager_v1_destroy(manager);
}
