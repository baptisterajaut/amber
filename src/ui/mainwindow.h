/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class Project;
class EffectControls;
class Viewer;
class Timeline;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent);
  ~MainWindow() override;

  /**
   * @brief Update window title
   *
   * Updates the window title to reflect the current project filename. Call if the project filename changes.
   *
   * NOTE: It's recommended to use update_project_filename() from Olive::Global to update the filename completely
   * instead of calling this function directly (update_project_filename() calls this function in the process).
   */
  void updateTitle();

  /**
   * @brief Load shortcut file.
   *
   * Loads a shortcut configuration from file and sets Olive to use them.
   *
   * @param fn
   *
   * URL of the shortcut file to be loaded
   *
   */
  void load_shortcuts(const QString& fn);

  /**
   * @brief Save shortcut file.
   *
   * Saves the current shortcut configuration to file. Only saves shortcuts that have been changed from default.
   *
   * @param fn
   *
   * URL to save the shortcut file to.
   */
  void save_shortcuts(const QString& fn);

  /**
   * @brief Load a CSS/QSS style from file to customize Olive's interface.
   *
   * @param fn
   *
   * URL to load the CSS file from.
   *
   * @return
   *
   * **TRUE** if CSS was successfully loaded.
   */
  bool load_css_from_file(const QString& fn);

  /**
   * @brief Set application's QStyle based on values from Config
   */
  void Restyle();

 public slots:
  /**
   * @brief Toggles full screen mode.
   *
   * Toggles the main window between full screen and windowed modes.
   */
  void toggle_full_screen();

 signals:
  /**
   * @brief Signal emitted once when the main window has finished initializing
   *
   * Emitted the first time paintEvent runs. Connect this to functions that must be completed post-initialization.
   */
  void finished_first_paint();

 protected:
  /**
   * @brief Close event
   *
   * Confirms whether the project can be closed, and if so performs various clean-up functions before the application
   * exits. It's preferable to call clean-up functions here rather than in the destructor because this will get called
   * first.
   */
  void closeEvent(QCloseEvent*) override;

  /**
   * @brief Paint event
   *
   * Overridden to provide the finished_first_paint() signal.
   */
  void paintEvent(QPaintEvent*) override;

  /**
   * @brief Change event
   *
   * Overridden to handle language changes (e->type() == QEvent::LanguageChange) by calling Retranslate().
   *
   * See documentation for QWidget::changeEvent() for more information.
   *
   * @param e
   * @return
   */
  void changeEvent(QEvent* e) override;

 private slots:
  void set_preview_resolution();

  /**
   * @brief Maximizes the currently hovered panel.
   *
   * Saves the current state of the panels/dock widgets and removes all except the currently hovered panel,
   * effectively maximizing the panel to the entire window.
   */
  void maximize_panel();

  /**
   * @brief Reset panel layout to default.
   *
   * Resets the current panel layout to default. Doesn't save the current layout.
   */
  void reset_layout();

  /**
   * @brief Function to prepare File menu.
   *
   * Primarily used to populate the Open Recent Projects menu.
   */
  void fileMenu_About_To_Be_Shown();

  /**
   * @brief Function to prepare Edit menu.
   *
   * Primarily used to set the enabled state on Undo and Redo depending if there are undos/redos available.
   */
  void editMenu_About_To_Be_Shown();

  /**
   * @brief Function to prepare Window menu.
   *
   * Primarily used to set the checked state of menu items corresponding to the panels that are currently visible.
   */
  void windowMenu_About_To_Be_Shown();

  /**
   * @brief Function to prepare Playback menu.
   *
   * Primarily used to set the checked state on the "Loop" item.
   */
  void playbackMenu_About_To_Be_Shown();

  /**
   * @brief Function to prepare View menu.
   *
   * Primarily used to set the checked state of various options in the view menu (e.g. title safe area, timecode
   * units, etc.)
   */
  void viewMenu_About_To_Be_Shown();

  /**
   * @brief Function to prepare Tools menu.
   *
   * Primarily used to set the checked state on various settings available from the Tools menu.
   */
  void toolMenu_About_To_Be_Shown();

  /**
   * @brief Toggle whether a panel is visible or not.
   *
   * Assumes the sender() QAction has a pointer to a QDockWidget in its data variable. Casts it and toggles its
   * visibility.
   */
  void toggle_panel_visibility();

  /**
   * @brief Open the Relink Media dialog to find missing media files.
   */
  void relink_media();
  void import_subtitle();

  /**
   * @brief Set panel lock
   *
   * @param locked
   *
   * If **TRUE** prevents panels from being moved around. Defaults to **FALSE**.
   */
  void set_panels_locked(bool locked);
  void toggle_color_labels();

 private:
  /**
   * @brief Internal function for setting the panel layout to a predetermined preset.
   *
   * Resets layout to default and optionally loads a layout from file. If loading from file, this function will
   * always load from `get_config_path() + "/layout"`.
   *
   * @param reset
   *
   * **TRUE** if this function should just reset the current layout. **FALSE** if it should load from the
   * aforementioned layout file.
   */
  void setup_layout(bool reset);

  /**
   * @brief Initialize menu bar menus and items.
   *
   * Internal initialization function for all menus and menu items in the main window. Called once from the
   * MainWindow() constructor.
   */
  void setup_menus();

  void Retranslate();

  // file menu actions
  QMenu* file_menu = nullptr;
  QMenu* new_menu = nullptr;
  QAction* open_project = nullptr;
  QMenu* open_recent = nullptr;
  QAction* open_action = nullptr;
  QAction* clear_open_recent_action = nullptr;
  QAction* save_project = nullptr;
  QAction* save_project_as = nullptr;
  QAction* import_action = nullptr;
  QAction* import_subtitle_action = nullptr;
  QAction* relink_media_action = nullptr;
  QAction* export_action = nullptr;
  QAction* export_frame_action = nullptr;
  QAction* exit_action = nullptr;

  // edit menu actions
  QMenu* edit_menu = nullptr;
  QAction* undo_action = nullptr;
  QAction* redo_action = nullptr;
  QAction* select_all_action = nullptr;
  QAction* deselect_all_action = nullptr;
  QAction* ripple_to_in_point_ = nullptr;
  QAction* ripple_to_out_point_ = nullptr;
  QAction* edit_to_in_point_ = nullptr;
  QAction* edit_to_out_point_ = nullptr;
  QAction* delete_inout_point_ = nullptr;
  QAction* ripple_delete_inout_point_ = nullptr;
  QAction* setedit_marker_ = nullptr;

  // view menu actions
  QMenu* view_menu = nullptr;
  QAction* zoom_in_ = nullptr;
  QAction* zoom_out_ = nullptr;
  QAction* increase_track_height_ = nullptr;
  QAction* decrease_track_height_ = nullptr;
  QAction* track_lines = nullptr;
  QAction* frames_action = nullptr;
  QAction* drop_frame_action = nullptr;
  QAction* nondrop_frame_action = nullptr;
  QAction* milliseconds_action = nullptr;
  QAction* no_autoscroll = nullptr;
  QAction* page_autoscroll = nullptr;
  QAction* smooth_autoscroll = nullptr;

  QMenu* preview_resolution_menu = nullptr;
  QAction* preview_res_full_ = nullptr;
  QAction* preview_res_half_ = nullptr;
  QAction* preview_res_quarter_ = nullptr;

  QMenu* title_safe_area_menu = nullptr;
  QAction* title_safe_off = nullptr;
  QAction* title_safe_default = nullptr;
  QAction* title_safe_43 = nullptr;
  QAction* title_safe_169 = nullptr;
  QAction* title_safe_custom = nullptr;

  QAction* full_screen = nullptr;
  QAction* full_screen_viewer_ = nullptr;
  QAction* show_all = nullptr;
  QAction* show_guides_ = nullptr;
  QAction* lock_guides_ = nullptr;
  QMenu* guides_menu_ = nullptr;

  // playback menu
  QMenu* playback_menu = nullptr;

  QAction* go_to_start_ = nullptr;
  QAction* previous_frame_ = nullptr;
  QAction* playpause_ = nullptr;
  QAction* play_in_to_out_ = nullptr;
  QAction* next_frame_ = nullptr;
  QAction* go_to_end_ = nullptr;
  QAction* previous_frames_ = nullptr;
  QAction* next_frames_ = nullptr;
  QAction* go_to_prev_cut_ = nullptr;
  QAction* go_to_next_cut_ = nullptr;
  QAction* go_to_in_point_ = nullptr;
  QAction* go_to_out_point_ = nullptr;
  QAction* shuttle_left_ = nullptr;
  QAction* shuttle_stop_ = nullptr;
  QAction* shuttle_right_ = nullptr;
  QAction* loop_action_ = nullptr;

  // window menu

  QMenu* window_menu = nullptr;

  QAction* window_project_action = nullptr;
  QAction* window_effectcontrols_action = nullptr;
  QAction* window_timeline_action = nullptr;
  QAction* window_graph_editor_action = nullptr;
  QAction* window_footageviewer_action = nullptr;
  QAction* window_sequenceviewer_action = nullptr;
  QAction* window_undo_history_action = nullptr;

  QAction* maximize_panel_ = nullptr;
  QAction* lock_panels_ = nullptr;
  QAction* reset_default_layout_ = nullptr;

  // tools menu
  QMenu* tools_menu = nullptr;

  QAction* pointer_tool_action = nullptr;
  QAction* edit_tool_action = nullptr;
  QAction* ripple_tool_action = nullptr;
  QAction* razor_tool_action = nullptr;
  QAction* slip_tool_action = nullptr;
  QAction* slide_tool_action = nullptr;
  QAction* track_select_tool_action = nullptr;
  QAction* hand_tool_action = nullptr;
  QAction* transition_tool_action = nullptr;
  QAction* snap_toggle = nullptr;
  QAction* color_labels_toggle = nullptr;
  QAction* rectified_waveforms = nullptr;
  QAction* autocut_silence_ = nullptr;
  QAction* preferences_action_ = nullptr;
  QAction* clear_undo_action_ = nullptr;

  // help menu
  QMenu* help_menu = nullptr;
  QAction* action_search_ = nullptr;
  QAction* debug_log_ = nullptr;
  QAction* about_action_ = nullptr;

  // used to store the panel state when one panel is maximized
  QByteArray temp_panel_state;

  // used in paintEvent() to determine the first paintEvent() performed
  bool first_show{true};
};

namespace amber {
extern MainWindow* MainWindow;
}

#endif  // MAINWINDOW_H
