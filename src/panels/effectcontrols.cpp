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

#include "effectcontrols.h"

#include <QVBoxLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QPushButton>
#include <QSpacerItem>
#include <QLabel>
#include <QVBoxLayout>
#include <QIcon>
#include <QApplication>

#include "panels/panels.h"
#include "effects/effect.h"
#include "effects/effectloaders.h"
#include "effects/transition.h"
#include "engine/clip.h"
#include "ui/collapsiblewidget.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "panels/project.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "panels/grapheditor.h"
#include "ui/viewerwidget.h"
#include "ui/menuhelper.h"
#include "ui/icons.h"
#include "project/clipboard.h"
#include "project/clipboard_serializer.h"
#include "global/config.h"
#include "ui/timelineheader.h"
#include "ui/keyframeview.h"
#include "ui/resizablescrollbar.h"
#include "global/debug.h"
#include "ui/menu.h"

EffectControls::EffectControls(QWidget *parent) :
  Panel(parent)
  
{
  setup_ui();
  Retranslate();

  Clear(false);

  headers->viewer = panel_sequence_viewer;
  headers->snapping = false;

  effects_area->parent_widget = scrollArea;
  effects_area->keyframe_area = keyframeView;
  effects_area->header = headers;
  keyframeView->header = headers;

  connect(keyframeView, &KeyframeView::wheel_event_signal, effects_area, &EffectsArea::receive_wheel_event);
  connect(horizontalScrollBar, &ResizableScrollBar::valueChanged, headers, &TimelineHeader::set_scroll);
  connect(horizontalScrollBar, &ResizableScrollBar::resize_move, keyframeView, &KeyframeView::resize_move);
  connect(horizontalScrollBar, &ResizableScrollBar::valueChanged, keyframeView, &KeyframeView::set_x_scroll);
  connect(verticalScrollBar, &QScrollBar::valueChanged, keyframeView, &KeyframeView::set_y_scroll);
  connect(verticalScrollBar, &QScrollBar::valueChanged, scrollArea->verticalScrollBar(), &QScrollBar::setValue);
  connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, verticalScrollBar, &QScrollBar::setValue);
}

EffectControls::~EffectControls()
{
  Clear(true);
}

bool EffectControls::keyframe_focus() {
  return headers->hasFocus() || keyframeView->hasFocus();
}

void EffectControls::set_zoom(bool in) {
  zoom *= (in) ? 2 : 0.5;
  update_keyframes();
  if (amber::ActiveSequence != nullptr) {
    scroll_to_frame(amber::ActiveSequence->playhead);
  }
}

void EffectControls::menu_select(QAction* q) {
  ComboAction* ca = new ComboAction(tr("Add Effect"));
  for (auto c : selected_clips_) {
    if ((c->track() < 0) == (effect_menu_subtype == EFFECT_TYPE_VIDEO)) {
      const EffectMeta* meta = reinterpret_cast<const EffectMeta*>(q->data().value<quintptr>());
      if (effect_menu_type == EFFECT_TYPE_TRANSITION) {
        if (c->opening_transition == nullptr) {
          ca->append(new AddTransitionCommand(c,
                                              nullptr,
                                              nullptr,
                                              meta,
                                              amber::CurrentConfig.default_transition_length));
        }
        if (c->closing_transition == nullptr) {
          ca->append(new AddTransitionCommand(nullptr,
                                              c,
                                              nullptr,
                                              meta,
                                              amber::CurrentConfig.default_transition_length));
        }
      } else {
        ca->append(new AddEffectCommand(c, nullptr, meta));
      }
    }
  }
  amber::UndoStack.push(ca);
  if (effect_menu_type == EFFECT_TYPE_TRANSITION) {
    update_ui(true);
  } else {
    Reload();
    if (panel_sequence_viewer != nullptr) {
      panel_sequence_viewer->viewer_widget->frame_update();
    }
  }
}

void EffectControls::update_keyframes() {
  for (auto ui : open_effects_) {
    ui->UpdateFromEffect();
  }

  headers->update_zoom(zoom);
  keyframeView->update();
}

void EffectControls::delete_selected_keyframes() {
  keyframeView->delete_selected_keyframes();
}

void EffectControls::copy(bool del) {
  bool cleared = false;

  ComboAction* ca = nullptr;
  if (del) {
    ca = new ComboAction(tr("Cut Effect(s)"));
  }

  for (auto open_effect : open_effects_) {
    if (open_effect->IsSelected()) {
      Effect* e = open_effect->GetEffect();

      if (e->meta->type == EFFECT_TYPE_EFFECT) {

        if (!cleared) {
          clear_clipboard();
          cleared = true;
          clipboard_type = CLIPBOARD_TYPE_EFFECT;
        }

        clipboard.append(e->copy(nullptr));

        if (del) {

          DeleteEffect(ca, e);

        }

      }
    }
  }

  if (del) {
    if (ca->hasActions()) {
      amber::UndoStack.push(ca);
    } else {
      delete ca;
    }
  }

  amber::push_clipboard_to_system();
}

void EffectControls::scroll_to_frame(long frame) {
  scroll_to_frame_internal(horizontalScrollBar, frame - keyframeView->visible_in, zoom, keyframeView->width());
}

void EffectControls::cut() {
  copy(true);
}

void EffectControls::show_effect_menu(int type, int subtype) {
  effect_menu_type = type;
  effect_menu_subtype = subtype;

  effects_loaded_mutex.lock();

  Menu effects_menu(this);
  effects_menu.setToolTipsVisible(true);

  for (const auto & em : effects) {
    if (em.type == type && em.subtype == subtype) {
      QAction* action = new QAction(&effects_menu);
      action->setText(em.name);
      action->setData(reinterpret_cast<quintptr>(&em));
      if (!em.tooltip.isEmpty()) {
        action->setToolTip(em.tooltip);
      }

      QMenu* parent = &effects_menu;
      if (!em.category.isEmpty()) {
        bool found = false;
        for (int j=0;j<effects_menu.actions().size();j++) {
          QAction* action = effects_menu.actions().at(j);
          if (action->menu() != nullptr) {
            if (action->menu()->title() == em.category) {
              parent = action->menu();
              found = true;
              break;
            }
          }
        }
        if (!found) {
          parent = new Menu(&effects_menu);
          parent->setToolTipsVisible(true);
          parent->setTitle(em.category);

          bool found = false;
          for (int i=0;i<effects_menu.actions().size();i++) {
            QAction* comp_action = effects_menu.actions().at(i);
            if (comp_action->text() > em.category) {
              effects_menu.insertMenu(comp_action, parent);
              found = true;
              break;
            }
          }
          if (!found) effects_menu.addMenu(parent);
        }
      }

      bool found = false;
      for (int i=0;i<parent->actions().size();i++) {
        QAction* comp_action = parent->actions().at(i);
        if (comp_action->text() > action->text()) {
          parent->insertAction(comp_action, action);
          found = true;
          break;
        }
      }
      if (!found) parent->addAction(action);
    }
  }

  effects_loaded_mutex.unlock();

  connect(&effects_menu, &QMenu::triggered, this, &EffectControls::menu_select);
  effects_menu.exec(QCursor::pos());
}

void EffectControls::Clear(bool clear_cache) {
  // clear existing clips
  deselect_all_effects(nullptr);

  for (auto open_effect : open_effects_) {
    delete open_effect;
  }
  open_effects_.clear();
  keyframeView->SetEffects(open_effects_);

  vcontainer->setVisible(false);
  acontainer->setVisible(false);
  headers->setVisible(false);
  keyframeView->setEnabled(false);

  if (clear_cache) {
    selected_clips_.clear();
  }

  UpdateTitle();
}

bool EffectControls::IsEffectSelected(Effect *e)
{
  for (auto open_effect : open_effects_) {
    if (open_effect->GetEffect() == e && open_effect->IsSelected()) {
      return true;
    }
  }
  return false;
}

void EffectControls::deselect_all_effects(QWidget* sender) {

  for (auto open_effect : open_effects_) {
    if (open_effect != sender) {
      open_effect->header_click(false, false);
    }
  }

  if (panel_sequence_viewer != nullptr) {
    panel_sequence_viewer->viewer_widget->update();
  }
}

void EffectControls::open_effect(QVBoxLayout* layout, Effect* e) {
  EffectUI* container = new EffectUI(e);

  connect(container, &EffectUI::CutRequested, this, &EffectControls::cut);
  connect(container, &EffectUI::CopyRequested, this, [this]() { copy(); });
  connect(container, &EffectUI::deselect_others, this, &EffectControls::deselect_all_effects);
  connect(container, &EffectUI::visibleChanged, this, [this]() { SyncLabelColumnWidth(); });

  open_effects_.append(container);

  layout->addWidget(container);
}

void EffectControls::UpdateTitle() {
  if (selected_clips_.isEmpty()) {
    setWindowTitle(panel_name + tr("(none)"));
  } else {
    setWindowTitle(panel_name + selected_clips_.first()->name());
  }
}

void EffectControls::setup_ui() {
  QWidget* contents = new QWidget(this);

  QHBoxLayout* hlayout = new QHBoxLayout(contents);
  hlayout->setSpacing(0);
  hlayout->setContentsMargins(0, 0, 0, 0);

  splitter = new QSplitter();
  splitter->setOrientation(Qt::Horizontal);
  splitter->setChildrenCollapsible(false);

  scrollArea = new QScrollArea();
  scrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setFrameShadow(QFrame::Plain);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  scrollArea->setWidgetResizable(true);

  QWidget* scrollAreaWidgetContents = new QWidget();

  QHBoxLayout* scrollAreaLayout = new QHBoxLayout(scrollAreaWidgetContents);
  scrollAreaLayout->setSpacing(0);
  scrollAreaLayout->setContentsMargins(0, 0, 0, 0);

  effects_area = new EffectsArea();
  effects_area->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(effects_area, &QWidget::customContextMenuRequested, this, &EffectControls::effects_area_context_menu);

  QVBoxLayout* effects_area_layout = new QVBoxLayout(effects_area);
  effects_area_layout->setSpacing(0);
  effects_area_layout->setContentsMargins(0, 0, 0, 0);

  vcontainer = new QWidget();
  QVBoxLayout* vcontainerLayout = new QVBoxLayout(vcontainer);
  vcontainerLayout->setSpacing(0);
  vcontainerLayout->setContentsMargins(0, 0, 0, 0);

  QWidget* veHeader = new QWidget();
  veHeader->setObjectName(QStringLiteral("veHeader"));
  veHeader->setStyleSheet(QLatin1String("#veHeader { background: rgba(0, 0, 0, 0.25); }"));

  QHBoxLayout* veHeaderLayout = new QHBoxLayout(veHeader);
  veHeaderLayout->setSpacing(0);
  veHeaderLayout->setContentsMargins(0, 0, 0, 0);

  QIcon add_effect_icon = amber::icon::CreateIconFromSVG(":/icons/add-effect.svg", false);
  QIcon add_transition_icon = amber::icon::CreateIconFromSVG(":/icons/add-transition.svg", false);

  btnAddVideoEffect = new QPushButton();
  btnAddVideoEffect->setIcon(add_effect_icon);
  veHeaderLayout->addWidget(btnAddVideoEffect);
  connect(btnAddVideoEffect, &QPushButton::clicked, this, &EffectControls::video_effect_click);

  veHeaderLayout->addStretch();

  lblVideoEffects = new QLabel();
  QFont font;
  font.setPointSize(9);
  lblVideoEffects->setFont(font);
  lblVideoEffects->setAlignment(Qt::AlignCenter);
  veHeaderLayout->addWidget(lblVideoEffects);

  veHeaderLayout->addStretch();

  btnAddVideoTransition = new QPushButton();
  btnAddVideoTransition->setIcon(add_transition_icon);
  connect(btnAddVideoTransition, &QPushButton::clicked, this, &EffectControls::video_transition_click);
  veHeaderLayout->addWidget(btnAddVideoTransition);

  vcontainerLayout->addWidget(veHeader);

  video_effect_area = new QWidget();
  video_effect_layout = new QVBoxLayout(video_effect_area);
  video_effect_layout->setSpacing(0);
  video_effect_layout->setContentsMargins(0, 0, 0, 0);

  vcontainerLayout->addWidget(video_effect_area);

  effects_area_layout->addWidget(vcontainer);

  acontainer = new QWidget();
  QVBoxLayout* acontainerLayout = new QVBoxLayout(acontainer);
  acontainerLayout->setSpacing(0);
  acontainerLayout->setContentsMargins(0, 0, 0, 0);
  QWidget* aeHeader = new QWidget();
  aeHeader->setObjectName(QStringLiteral("aeHeader"));
  aeHeader->setStyleSheet(QLatin1String("#aeHeader { background: rgba(0, 0, 0, 0.25); }"));

  QHBoxLayout* aeHeaderLayout = new QHBoxLayout(aeHeader);
  aeHeaderLayout->setSpacing(0);
  aeHeaderLayout->setContentsMargins(0, 0, 0, 0);

  btnAddAudioEffect = new QPushButton();
  btnAddAudioEffect->setIcon(add_effect_icon);
  connect(btnAddAudioEffect, &QPushButton::clicked, this, &EffectControls::audio_effect_click);
  aeHeaderLayout->addWidget(btnAddAudioEffect);

  aeHeaderLayout->addStretch();

  lblAudioEffects = new QLabel();
  lblAudioEffects->setFont(font);
  lblAudioEffects->setAlignment(Qt::AlignCenter);
  aeHeaderLayout->addWidget(lblAudioEffects);

  aeHeaderLayout->addStretch();

  btnAddAudioTransition = new QPushButton();
  btnAddAudioTransition->setIcon(add_transition_icon);
  connect(btnAddAudioTransition, &QPushButton::clicked, this, &EffectControls::audio_transition_click);
  aeHeaderLayout->addWidget(btnAddAudioTransition);

  acontainerLayout->addWidget(aeHeader);

  audio_effect_area = new QWidget();
  audio_effect_layout = new QVBoxLayout(audio_effect_area);
  audio_effect_layout->setSpacing(0);
  audio_effect_layout->setContentsMargins(0, 0, 0, 0);

  acontainerLayout->addWidget(audio_effect_area);

  effects_area_layout->addWidget(acontainer);

  effects_area_layout->addStretch();

  scrollAreaLayout->addWidget(effects_area);

  scrollArea->setWidget(scrollAreaWidgetContents);
  splitter->addWidget(scrollArea);

  QWidget* keyframeArea = new QWidget();

  QSizePolicy keyframe_sp;
  keyframe_sp.setHorizontalPolicy(QSizePolicy::Minimum);
  keyframe_sp.setVerticalPolicy(QSizePolicy::Preferred);
  keyframe_sp.setHorizontalStretch(1);
  keyframeArea->setSizePolicy(keyframe_sp);

  QVBoxLayout* keyframeAreaLayout = new QVBoxLayout(keyframeArea);
  keyframeAreaLayout->setSpacing(0);
  keyframeAreaLayout->setContentsMargins(0, 0, 0, 0);

  headers = new TimelineHeader();
  keyframeAreaLayout->addWidget(headers);

  QWidget* keyframeCenterWidget = new QWidget();

  QHBoxLayout* keyframeCenterLayout = new QHBoxLayout(keyframeCenterWidget);
  keyframeCenterLayout->setSpacing(0);
  keyframeCenterLayout->setContentsMargins(0, 0, 0, 0);

  keyframeView = new KeyframeView();

  keyframeCenterLayout->addWidget(keyframeView);

  verticalScrollBar = new QScrollBar();
  verticalScrollBar->setOrientation(Qt::Vertical);

  keyframeCenterLayout->addWidget(verticalScrollBar);


  keyframeAreaLayout->addWidget(keyframeCenterWidget);

  horizontalScrollBar = new ResizableScrollBar();
  horizontalScrollBar->setOrientation(Qt::Horizontal);

  keyframeAreaLayout->addWidget(horizontalScrollBar);

  splitter->addWidget(keyframeArea);

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  hlayout->addWidget(splitter);

  setWidget(contents);
}

void EffectControls::Retranslate() {
  panel_name = tr("Effects: ");

  btnAddVideoEffect->setToolTip(tr("Add Video Effect"));
  lblVideoEffects->setText(tr("VIDEO EFFECTS"));
  btnAddVideoTransition->setToolTip(tr("Add Video Transition"));
  btnAddAudioEffect->setToolTip(tr("Add Audio Effect"));
  lblAudioEffects->setText(tr("AUDIO EFFECTS"));
  btnAddAudioTransition->setToolTip(tr("Add Audio Transition"));

  UpdateTitle();
}

void EffectControls::LoadLayoutState(const QByteArray &data)
{
  splitter->restoreState(data);
}

QByteArray EffectControls::SaveLayoutState()
{
  return splitter->saveState();
}

void EffectControls::update_scrollbar() {
  verticalScrollBar->setMaximum(qMax(0, effects_area->height() - keyframeView->height() - headers->height()));
  verticalScrollBar->setPageStep(verticalScrollBar->height());
}

void EffectControls::queue_post_update() {
  keyframeView->update();
  update_scrollbar();
}

void EffectControls::effects_area_context_menu() {
  Menu menu(this);

  amber::MenuHelper.create_effect_paste_action(&menu);

  menu.exec(QCursor::pos());
}

void EffectControls::DeleteEffect(ComboAction* ca, Effect* effect_ref) {
  if (effect_ref->meta->type == EFFECT_TYPE_EFFECT) {

    ca->append(new EffectDeleteCommand(effect_ref));

  } else if (effect_ref->meta->type == EFFECT_TYPE_TRANSITION) {

    // Retrieve shared ptr for this transition

    Clip* attached_clip = effect_ref->parent_clip;

    TransitionPtr t = nullptr;

    if (attached_clip->opening_transition.get() == effect_ref) {

      t = attached_clip->opening_transition;

    } else if (attached_clip->closing_transition.get() == effect_ref) {

      t = attached_clip->closing_transition;

    }

    if (t == nullptr) {

      qWarning() << "Failed to delete transition, couldn't find clip link.";

    } else {

      ca->append(new DeleteTransitionCommand(t));

    }

  }
}

void EffectControls::DeleteSelectedEffects() {
  ComboAction* ca = new ComboAction(tr("Delete Effect(s)"));

  for (auto open_effect : open_effects_) {
    if (open_effect->IsSelected()) {
      DeleteEffect(ca, open_effect->GetEffect());
    }
  }

  if (ca->hasActions()) {
    amber::UndoStack.push(ca);
    update_ui(true);
  } else {
    delete ca;
  }
}

void EffectControls::Reload() {
  Clear(false);
  Load();
}

void EffectControls::SyncLabelColumnWidth() {
  int max_w = 0;
  for (EffectUI* ui : open_effects_) {
    if (!ui->IsExpanded()) continue;
    for (QLabel* lbl : ui->labels()) {
      max_w = qMax(max_w, lbl->sizeHint().width());
    }
  }
  for (EffectUI* ui : open_effects_) {
    for (QLabel* lbl : ui->labels()) {
      lbl->setMinimumWidth(max_w);
    }
  }
}

void EffectControls::SetClips()
{
  if (amber::ActiveSequence == nullptr) {
    Clear(true);
    selected_clips_.clear();
    return;
  }

  QVector<Clip*> new_clips = amber::ActiveSequence->SelectedClips(false);

  // If the selected clips haven't changed, skip the expensive teardown/rebuild
  if (new_clips == selected_clips_) {
    return;
  }

  Clear(true);
  selected_clips_ = new_clips;
  Load();
}

void EffectControls::Load() {
  bool graph_editor_row_is_still_active = false;

  // load in new clips
  for (auto c : selected_clips_) {
    QVBoxLayout* layout;

    if (c->track() < 0) {
      vcontainer->setVisible(true);
      layout = video_effect_layout;
    } else {
      acontainer->setVisible(true);
      layout = audio_effect_layout;
    }

    // Create a list of the effects we'll open
    QVector<Effect*> effects_to_open;

    // Determine based on the current selections whether to load all effects or just the transitions
    bool whole_clip_is_selected = c->IsSelected();

    if (whole_clip_is_selected) {
      for (const auto & effect : c->effects) {
        effects_to_open.append(effect.get());
      }
    }
    if (c->opening_transition != nullptr
        && (whole_clip_is_selected || c->sequence->IsTransitionSelected(c->opening_transition.get()))) {
      effects_to_open.append(c->opening_transition.get());
    }
    if (c->closing_transition != nullptr
        && (whole_clip_is_selected || c->sequence->IsTransitionSelected(c->closing_transition.get()))) {
      effects_to_open.append(c->closing_transition.get());
    }

    for (auto j : effects_to_open) {

      // Check if we've already opened an effect of this type before
      bool already_opened = false;
      for (auto open_effect : open_effects_) {
        if (open_effect->GetEffect()->meta == j->meta
            && !open_effect->IsAttachedToClip(c)) {

          open_effect->AddAdditionalEffect(j);

          already_opened = true;

          break;
        }
      }

      if (!already_opened) {
        open_effect(layout, j);
      }

      // Check if one of the open effects contains the row currently active in the graph editor. If not, we'll have
      // to clear the graph editor later.
      if (!graph_editor_row_is_still_active) {
        for (int k=0;k<j->row_count();k++) {
          EffectRow* row = j->row(k);
          if (row == panel_graph_editor->get_row()) {
            graph_editor_row_is_still_active = true;
            break;
          }
        }
      }
    }
  }

  keyframeView->SetEffects(open_effects_);

  if (selected_clips_.size() > 0) {
    keyframeView->setEnabled(true);

    headers->setVisible(true);

    QTimer::singleShot(50, this, &EffectControls::queue_post_update);
  }

  // If the graph editor's currently active row is not part of the current effects, clear it
  if (!graph_editor_row_is_still_active) {
    panel_graph_editor->set_row(nullptr);
  }

  UpdateTitle();
  update_keyframes();

  SyncLabelColumnWidth();
}

void EffectControls::video_effect_click() {
  show_effect_menu(EFFECT_TYPE_EFFECT, EFFECT_TYPE_VIDEO);
}

void EffectControls::audio_effect_click() {
  show_effect_menu(EFFECT_TYPE_EFFECT, EFFECT_TYPE_AUDIO);
}

void EffectControls::video_transition_click() {
  show_effect_menu(EFFECT_TYPE_TRANSITION, EFFECT_TYPE_VIDEO);
}

void EffectControls::audio_transition_click() {
  show_effect_menu(EFFECT_TYPE_TRANSITION, EFFECT_TYPE_AUDIO);
}

void EffectControls::resizeEvent(QResizeEvent*) {
  update_scrollbar();
}

bool EffectControls::is_focused() {
  if (this->hasFocus()) return true;

  for (auto open_effect : open_effects_) {
    if (open_effect->IsFocused()) {
      return true;
    }
  }

  return false;
}

EffectsArea::EffectsArea(QWidget* parent) :
  QWidget(parent)
{}

void EffectsArea::resizeEvent(QResizeEvent *)
{
  if (!amber::CurrentConfig.effect_panel_shrinkable) {
    setMinimumWidth(sizeHint().width());
  } else {
    setMinimumWidth(0);
  }
}

void EffectsArea::receive_wheel_event(QWheelEvent *e) {
  QApplication::sendEvent(this, e);
}
