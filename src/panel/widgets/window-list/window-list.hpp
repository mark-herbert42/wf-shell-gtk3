#ifndef WIDGETS_WINDOW_LIST_HPP
#define WIDGETS_WINDOW_LIST_HPP

#include "../../widget.hpp"
#include "panel.hpp"
#include <wlr-foreign-toplevel-management-unstable-v1-client-protocol.h>
#include <ext-workspace-v1-client-protocol.h>
#include <xfce-foreign-toplevel-management-private-v1-client-protocol.h>
#include <wayfire/config/types.hpp>
#include "wf-option-wrap.hpp"

#include <gtkmm/button.h>
#include <gtkmm/scrolledwindow.h>

class WayfireToplevel;

class WayfireWindowListBox : public Gtk::HBox
{
    Gtk::Widget *top_widget = nullptr;
    int top_x = 0;

  public:
    WayfireWindowListBox();

    /**
     * Set the widget which should always be rendered on top of the other child
     * widgets */
    void set_top_widget(Gtk::Widget *top = nullptr);
    /** Set the absolute position of the top widget */
    void set_top_x(int x);

    /**
     * Override some of Gtk::HBox's built-in layouting functions, so that we
     * support manually dragging a button
     */
    void forall_vfunc(gboolean, GtkCallback callback, gpointer callback_data) override;
    void on_size_allocate(Gtk::Allocation& alloc) override;

    /**
     * @param x the x-axis position, relative to ref
     * @param ref The widget that x is relative to. ref must be a child
     * widget of this box
     * @return the position x, but relative to the box
     */
    int get_absolute_position(int x, Gtk::Widget& ref);

    /** Find the direct child widget at the given box-relative coordinates,
     * ignoring the top widget if possible, i.e if the top widget and some
     * other widget are at the given coordinates, then the bottom widget will
     * be returned
     *
     * @return The direct child widget or none if it doesn't exist
     */
    Gtk::Widget *get_widget_at(int x);

    /**
     * Get the list of widgets sorted from left to right, i.e ignoring the top
     * widget setting
     */
    std::vector<Gtk::Widget*> get_unsorted_widgets();
};

class WayfireWindowList : public WayfireWidget
{
  public:
    std::map<zwlr_foreign_toplevel_handle_v1*,
        std::unique_ptr<WayfireToplevel>> toplevels;

    zwlr_foreign_toplevel_manager_v1 *manager = NULL;
    WayfireOutput *output;
    WayfireWindowListBox box;
    Gtk::ScrolledWindow scrolled_window;

    /* ext-workspace + xfce bridge, used to filter by current workspace */
    ext_workspace_manager_v1 *ws_manager = nullptr;
    xfce_foreign_toplevel_manager_private_v1 *xfce_manager = nullptr;
    std::vector<ext_workspace_handle_v1*> ws_handles;
    std::map<ext_workspace_handle_v1*, uint32_t> ws_states;
    ext_workspace_handle_v1 *current_workspace = nullptr;
    /* The name of the active workspace on this output. The xfce bridge may
     * hand us an ext_workspace_handle_v1 object from a different client binding
     * than ours, so object identity cannot be used across monitors; the
     * compositor-assigned name is stable and shared, so we compare by name. */
    std::string current_workspace_name;
    WfOption<bool> only_current_workspace{"panel/only_current_workspace"};

    /* Workspaces are grouped per output. To know the *active workspace of this
     * output* (and not of some other monitor) we track which output each group
     * belongs to and which workspaces belong to each group. */
    std::vector<ext_workspace_group_handle_v1*> ws_groups;
    std::map<ext_workspace_group_handle_v1*, wl_output*> ws_group_output;
    std::map<ext_workspace_group_handle_v1*,
        std::vector<ext_workspace_handle_v1*>> ws_group_workspaces;

    WayfireWindowList(WayfireOutput *output);
    virtual ~WayfireWindowList();

    void handle_toplevel_manager(zwlr_foreign_toplevel_manager_v1 *manager);
    void handle_toplevel_closed(zwlr_foreign_toplevel_handle_v1 *handle);
    void handle_new_toplevel(zwlr_foreign_toplevel_handle_v1 *handle);

    void handle_ws_manager(ext_workspace_manager_v1 *manager);
    void handle_xfce_manager(xfce_foreign_toplevel_manager_private_v1 *manager);
    void handle_workspace_created(ext_workspace_handle_v1 *ws);
    void handle_workspace_state(ext_workspace_handle_v1 *ws, uint32_t state);
    void handle_workspace_removed(ext_workspace_handle_v1 *ws);
    void handle_workspace_done();

    void handle_workspace_group(ext_workspace_group_handle_v1 *group);
    void handle_group_output_enter(ext_workspace_group_handle_v1 *group,
        wl_output *output);
    void handle_group_output_leave(ext_workspace_group_handle_v1 *group,
        wl_output *output);
    void handle_group_workspace_enter(ext_workspace_group_handle_v1 *group,
        ext_workspace_handle_v1 *ws);
    void handle_group_workspace_leave(ext_workspace_group_handle_v1 *group,
        ext_workspace_handle_v1 *ws);
    void handle_group_removed(ext_workspace_group_handle_v1 *group);

    /** Recompute current_workspace as the active workspace of this output's group */
    void recompute_current_workspace();

    /** @return true if a toplevel on @ws should be shown in this list */
    bool on_current_workspace(ext_workspace_handle_v1 *ws);
    /** Re-evaluate the visibility of every toplevel button */
    void refresh_visibility();

    wayfire_config *get_config();

    void init(Gtk::HBox *container) override;
    void add_output(WayfireOutput *output);

  private:
    void on_draw(const Cairo::RefPtr<Cairo::Context>&);

    void set_button_width(int width);
    int get_default_button_width();
    int get_target_button_width();
};

#endif /* end of include guard: WIDGETS_WINDOW_LIST_HPP */
