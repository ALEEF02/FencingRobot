#include "ui/MainWindow.hpp"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace fencing::ui {
namespace {

constexpr auto kTimelineMimeType = "application/x-fencing-timeline-step";
constexpr double kTimelineSnapSeconds = 0.1;
constexpr double kDefaultPositioningDurationSeconds = 2.0;
constexpr double kMinimumPositioningDurationSeconds = 0.5;

class PaletteButton final : public QPushButton {
public:
    using PayloadProvider = std::function<QString()>;

    PaletteButton(QString text, PayloadProvider payload, QWidget* parent = nullptr)
        : QPushButton(std::move(text), parent), payload_(std::move(payload)) {
        setCursor(Qt::OpenHandCursor);
        setMinimumHeight(48);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) drag_origin_ = event->position().toPoint();
        QPushButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) ||
            (event->position().toPoint() - drag_origin_).manhattanLength() <
                QApplication::startDragDistance()) {
            QPushButton::mouseMoveEvent(event);
            return;
        }

        auto* mime = new QMimeData;
        mime->setData(kTimelineMimeType, payload_().toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        setCursor(Qt::ClosedHandCursor);
        drag->exec(Qt::CopyAction);
        setCursor(Qt::OpenHandCursor);
    }

private:
    PayloadProvider payload_;
    QPoint drag_origin_;
};

QLabel* sectionLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setProperty("role", "sectionLabel");
    return label;
}

QPushButton* actionButton(const QString& text, const char* role = "secondary") {
    auto* button = new QPushButton(text);
    button->setProperty("role", role);
    button->setMinimumHeight(42);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QString drillStateLabel(domain::DrillState state) {
    return QString::fromLatin1(domain::toString(state)).toUpper();
}

QString movementLabel(const std::string& movement_id) {
    QString label = QString::fromStdString(movement_id);
    label.replace('_', ' ');
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    return label;
}

} // namespace

class DrillTimelineWidget final : public QWidget {
public:
    using DurationLookup = std::function<double(const std::string&)>;
    using DropHandler =
        std::function<bool(bool, const QString&, double, double, double)>;
    using RemoveHandler = std::function<void(bool, std::size_t)>;
    using EditHandler =
        std::function<bool(bool, std::size_t, double, double)>;

    explicit DrillTimelineWidget(DurationLookup duration_lookup, QWidget* parent = nullptr)
        : QWidget(parent), duration_lookup_(std::move(duration_lookup)) {
        setAcceptDrops(true);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName("Drill timeline");
        setMinimumHeight(totalHeight());
    }

    void setDraft(domain::Drill* draft) {
        draft_ = draft;
        selected_.reset();
        drag_.reset();
        const int timeline_width =
            labelWidth() + static_cast<int>(std::ceil(horizonSeconds() * pixelsPerSecond()));
        setMinimumSize(timeline_width, totalHeight());
        resize(timeline_width, totalHeight());
        update();
    }

    void setDropHandler(DropHandler handler) { drop_handler_ = std::move(handler); }
    void setRemoveHandler(RemoveHandler handler) { remove_handler_ = std::move(handler); }
    void setEditHandler(EditHandler handler) { edit_handler_ = std::move(handler); }

    QSize sizeHint() const override {
        return {labelWidth() + static_cast<int>(horizonSeconds() * pixelsPerSecond()),
                totalHeight()};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#11141A"));

        const QRect ruler(labelWidth(), 0, width() - labelWidth(), rulerHeight());
        const QRect motion_lane(labelWidth(), rulerHeight(), width() - labelWidth(), laneHeight());
        const QRect positioning_lane(labelWidth(), rulerHeight() + laneHeight(),
                                     width() - labelWidth(), laneHeight());

        painter.fillRect(ruler, QColor("#161A21"));
        painter.fillRect(motion_lane, QColor("#141820"));
        painter.fillRect(positioning_lane, QColor("#101820"));
        painter.fillRect(QRect(0, 0, labelWidth(), totalHeight()), QColor("#191D25"));

        QPen divider(QColor("#343A46"));
        divider.setWidth(1);
        painter.setPen(divider);
        painter.drawLine(labelWidth(), 0, labelWidth(), totalHeight());
        painter.drawLine(0, rulerHeight(), width(), rulerHeight());
        painter.drawLine(0, rulerHeight() + laneHeight(), width(), rulerHeight() + laneHeight());

        QFont tick_font("JetBrains Mono");
        tick_font.setPixelSize(11);
        painter.setFont(tick_font);

        const int seconds = static_cast<int>(std::ceil(horizonSeconds()));
        for (int second = 0; second <= seconds; ++second) {
            const int x = labelWidth() + static_cast<int>(second * pixelsPerSecond());
            const bool major = second % 5 == 0;
            painter.setPen(QColor(major ? "#454C5B" : "#292F39"));
            painter.drawLine(x, major ? 13 : 26, x, totalHeight());
            if (major) {
                painter.setPen(QColor("#AEB6C5"));
                painter.drawText(QRect(x + 7, 8, 52, 22), Qt::AlignLeft | Qt::AlignVCenter,
                                 QString::number(second) + "s");
            }
        }

        QFont lane_font("IBM Plex Sans");
        lane_font.setPixelSize(12);
        lane_font.setBold(true);
        painter.setFont(lane_font);
        painter.setPen(QColor("#F1F3F7"));
        painter.drawText(QRect(18, rulerHeight(), labelWidth() - 30, laneHeight()),
                         Qt::AlignVCenter | Qt::AlignLeft, "MOTION");
        painter.drawText(QRect(18, rulerHeight() + laneHeight(), labelWidth() - 30, laneHeight()),
                         Qt::AlignVCenter | Qt::AlignLeft, "POSITIONING");

        if (!draft_) return;

        if (draft_->motion_steps.empty()) {
            drawEmptyHint(painter, motion_lane, "Drag blade motions here");
        }
        if (draft_->positioning_steps.empty()) {
            drawEmptyHint(painter, positioning_lane, "Drag forward or backward positioning here");
        }

        for (std::size_t index = 0; index < draft_->motion_steps.size(); ++index) {
            const auto& step = draft_->motion_steps[index];
            const double duration = duration_lookup_(step.movement_id);
            const double start = previewStart(false, index, step.start_time_seconds);
            const QRectF block = blockRect(false, start, duration);
            const bool selected = selected_ && !selected_->positioning &&
                                  selected_->index == index;
            drawBlock(painter, block, movementLabel(step.movement_id),
                      QString("%1s").arg(duration, 0, 'f', 1), false, selected);
        }

        for (std::size_t index = 0; index < draft_->positioning_steps.size(); ++index) {
            const auto& step = draft_->positioning_steps[index];
            const double start = previewStart(true, index, step.start_time_seconds);
            const double duration = previewDuration(index, step.duration_seconds);
            const QRectF block = blockRect(true, start, duration);
            const bool selected = selected_ && selected_->positioning &&
                                  selected_->index == index;
            const QString direction = step.speed_mps >= 0.0 ? "Forward" : "Backward";
            const double distance =
                std::round((std::abs(step.speed_mps * duration) + 1e-9) * 10.0) /
                10.0;
            drawBlock(painter, block, direction,
                      QString("%1 m/s · %2 m")
                          .arg(std::abs(step.speed_mps), 0, 'f', 2)
                          .arg(distance, 0, 'f', 1),
                      true, selected);
        }
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(kTimelineMimeType)) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        if (!drop_handler_) return;
        const QString payload =
            QString::fromUtf8(event->mimeData()->data(kTimelineMimeType));
        const QStringList parts = payload.split('|');
        if (parts.isEmpty()) return;

        const double raw_time =
            std::max(0.0, (event->position().x() - labelWidth()) / pixelsPerSecond());
        const double start_time =
            std::round(raw_time / kTimelineSnapSeconds) * kTimelineSnapSeconds;

        bool accepted = false;
        if (parts[0] == "motion" && parts.size() == 2) {
            accepted = drop_handler_(false, parts[1], 0.0, 0.0, start_time);
        } else if (parts[0] == "positioning" && parts.size() == 2) {
            accepted = drop_handler_(true, {}, parts[1].toDouble(),
                                     kDefaultPositioningDurationSeconds, start_time);
        }

        if (accepted) {
            event->acceptProposedAction();
            update();
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        selected_ = hitAt(event->position());
        if (selected_) {
            setFocus();
            if (event->button() == Qt::LeftButton) {
                const auto [start, duration] = timingFor(*selected_);
                drag_ = DragState{
                    .mode = dragModeFor(*selected_, event->position()),
                    .selection = *selected_,
                    .press_position = event->position(),
                    .original_start = start,
                    .original_duration = duration,
                    .preview_start = start,
                    .preview_duration = duration,
                };
                setCursor(drag_->mode == DragMode::Move
                              ? Qt::ClosedHandCursor
                              : Qt::SizeHorCursor);
                event->accept();
            }
        }
        update();
        if (!event->isAccepted()) QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!drag_ || !(event->buttons() & Qt::LeftButton)) {
            updateCursor(event->position());
            QWidget::mouseMoveEvent(event);
            return;
        }

        const double delta =
            (event->position().x() - drag_->press_position.x()) / pixelsPerSecond();
        if (drag_->mode == DragMode::Move) {
            drag_->preview_start = snapTime(
                std::max(0.0, drag_->original_start + delta));
        } else if (drag_->mode == DragMode::ResizeStart) {
            const double original_end =
                drag_->original_start + drag_->original_duration;
            drag_->preview_start = std::clamp(
                snapTime(drag_->original_start + delta), 0.0,
                original_end - kMinimumPositioningDurationSeconds);
            drag_->preview_duration = original_end - drag_->preview_start;
        } else {
            const double preview_end = std::max(
                drag_->original_start + kMinimumPositioningDurationSeconds,
                snapTime(drag_->original_start + drag_->original_duration + delta));
            drag_->preview_duration = preview_end - drag_->original_start;
        }
        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !drag_) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        const DragState completed = *drag_;
        drag_.reset();
        unsetCursor();
        const bool changed =
            std::abs(completed.preview_start - completed.original_start) > 0.001 ||
            std::abs(completed.preview_duration - completed.original_duration) > 0.001;
        if (changed && edit_handler_) {
            edit_handler_(completed.selection.positioning,
                          completed.selection.index,
                          completed.preview_start,
                          completed.preview_duration);
        }
        update();
        event->accept();
    }

    void leaveEvent(QEvent* event) override {
        if (!drag_) unsetCursor();
        QWidget::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
            selected_ && remove_handler_) {
            remove_handler_(selected_->positioning, selected_->index);
            selected_.reset();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        const auto hit = hitAt(event->pos());
        if (!hit || !remove_handler_) return;
        QMenu menu(this);
        auto* remove = menu.addAction("Remove step");
        if (menu.exec(event->globalPos()) == remove) {
            remove_handler_(hit->positioning, hit->index);
            selected_.reset();
        }
    }

private:
    struct Selection {
        bool positioning{};
        std::size_t index{};
    };

    enum class DragMode { Move, ResizeStart, ResizeEnd };

    struct DragState {
        DragMode mode{DragMode::Move};
        Selection selection;
        QPointF press_position;
        double original_start{};
        double original_duration{};
        double preview_start{};
        double preview_duration{};
    };

    static constexpr int labelWidth() { return 126; }
    static constexpr int rulerHeight() { return 44; }
    static constexpr int laneHeight() { return 110; }
    static constexpr int totalHeight() { return rulerHeight() + laneHeight() * 2; }
    static constexpr double pixelsPerSecond() { return 75.0; }
    static constexpr double resizeHandleWidth() { return 12.0; }

    static double snapTime(double seconds) {
        return std::round(seconds / kTimelineSnapSeconds) * kTimelineSnapSeconds;
    }

    double horizonSeconds() const {
        double horizon = 15.0;
        if (!draft_) return horizon;
        for (const auto& step : draft_->motion_steps) {
            horizon = std::max(horizon,
                               step.start_time_seconds + duration_lookup_(step.movement_id) + 2.0);
        }
        for (const auto& step : draft_->positioning_steps) {
            horizon = std::max(horizon,
                               step.start_time_seconds + step.duration_seconds + 2.0);
        }
        return std::ceil(horizon / 5.0) * 5.0;
    }

    QRectF blockRect(bool positioning, double start, double duration) const {
        const double x = labelWidth() + start * pixelsPerSecond() + 4.0;
        const double y = rulerHeight() + (positioning ? laneHeight() : 0) + 20.0;
        const double block_width = std::max(24.0, duration * pixelsPerSecond() - 8.0);
        return {x, y, block_width, laneHeight() - 40.0};
    }

    std::pair<double, double> timingFor(const Selection& selection) const {
        if (selection.positioning) {
            const auto& step = draft_->positioning_steps[selection.index];
            return {step.start_time_seconds, step.duration_seconds};
        }
        const auto& step = draft_->motion_steps[selection.index];
        return {step.start_time_seconds, duration_lookup_(step.movement_id)};
    }

    DragMode dragModeFor(const Selection& selection, const QPointF& point) const {
        if (!selection.positioning) return DragMode::Move;
        const auto [start, duration] = timingFor(selection);
        const QRectF block = blockRect(true, start, duration);
        const double handle_width =
            std::min(resizeHandleWidth(), block.width() / 3.0);
        const double left_distance = std::abs(point.x() - block.left());
        const double right_distance = std::abs(point.x() - block.right());
        if (left_distance <= handle_width && left_distance <= right_distance) {
            return DragMode::ResizeStart;
        }
        if (right_distance <= handle_width) return DragMode::ResizeEnd;
        return DragMode::Move;
    }

    double previewStart(bool positioning, std::size_t index, double fallback) const {
        return drag_ && drag_->selection.positioning == positioning &&
                       drag_->selection.index == index
                   ? drag_->preview_start
                   : fallback;
    }

    double previewDuration(std::size_t index, double fallback) const {
        return drag_ && drag_->selection.positioning && drag_->selection.index == index
                   ? drag_->preview_duration
                   : fallback;
    }

    void updateCursor(const QPointF& point) {
        const auto hit = hitAt(point);
        if (!hit) {
            unsetCursor();
            return;
        }
        setCursor(dragModeFor(*hit, point) == DragMode::Move
                      ? Qt::OpenHandCursor
                      : Qt::SizeHorCursor);
    }

    std::optional<Selection> hitAt(const QPointF& point) const {
        if (!draft_) return std::nullopt;
        for (std::size_t index = 0; index < draft_->motion_steps.size(); ++index) {
            const auto& step = draft_->motion_steps[index];
            if (blockRect(false, step.start_time_seconds,
                          duration_lookup_(step.movement_id)).contains(point)) {
                return Selection{false, index};
            }
        }
        for (std::size_t index = 0; index < draft_->positioning_steps.size(); ++index) {
            const auto& step = draft_->positioning_steps[index];
            if (blockRect(true, step.start_time_seconds,
                          step.duration_seconds).contains(point)) {
                return Selection{true, index};
            }
        }
        return std::nullopt;
    }

    void drawEmptyHint(QPainter& painter, const QRect& lane, const QString& text) const {
        QFont font("IBM Plex Sans");
        font.setPixelSize(13);
        painter.setFont(font);
        painter.setPen(QColor("#6F7888"));
        painter.drawText(lane.adjusted(24, 0, -24, 0), Qt::AlignCenter, text);
    }

    void drawBlock(QPainter& painter, const QRectF& rect, const QString& title,
                   const QString& detail, bool positioning, bool selected) const {
        const QColor fill(positioning ? "#173447" : "#343027");
        const QColor border(positioning ? "#4BA3D9" : "#D8B45B");
        painter.setBrush(fill);
        QPen pen(selected ? QColor("#F7F8FA") : border);
        pen.setWidth(selected ? 2 : 1);
        painter.setPen(pen);
        painter.drawRoundedRect(rect, 8, 8);

        if (positioning) {
            QPen handle_pen(border);
            handle_pen.setWidth(2);
            painter.setPen(handle_pen);
            const double top = rect.top() + 18.0;
            const double bottom = rect.bottom() - 18.0;
            painter.drawLine(QPointF(rect.left() + 7.0, top),
                             QPointF(rect.left() + 7.0, bottom));
            painter.drawLine(QPointF(rect.right() - 7.0, top),
                             QPointF(rect.right() - 7.0, bottom));
        }

        QFont title_font("IBM Plex Sans");
        title_font.setPixelSize(14);
        title_font.setBold(true);
        painter.setFont(title_font);
        painter.setPen(QColor("#F4F6F9"));
        const double horizontal_inset = positioning ? 17.0 : 13.0;
        painter.drawText(rect.adjusted(horizontal_inset, 10, -horizontal_inset, -28),
                         Qt::AlignLeft | Qt::AlignTop, title);

        QFont detail_font("JetBrains Mono");
        detail_font.setPixelSize(10);
        painter.setFont(detail_font);
        painter.setPen(QColor(positioning ? "#A9D5EE" : "#E5D19B"));
        painter.drawText(rect.adjusted(horizontal_inset, 36, -horizontal_inset, -8),
                         Qt::AlignLeft | Qt::AlignBottom, detail);
    }

    domain::Drill* draft_{};
    DurationLookup duration_lookup_;
    DropHandler drop_handler_;
    RemoveHandler remove_handler_;
    EditHandler edit_handler_;
    std::optional<Selection> selected_;
    std::optional<DragState> drag_;
};

MainWindow::MainWindow(ports::driving::ForManagingDrills& drills,
                       ports::driving::ForDrillOperation& control,
                       ports::driving::ForConfiguringRobot& configuring,
                       ports::driving::ForMonitoringRobot& monitoring,
                       QString backend_label, QWidget* parent)
    : QMainWindow(parent), drills_(drills), control_(control),
      configuring_(configuring), monitoring_(monitoring),
      movements_(drills.listMovements()) {
    setWindowTitle(QString("Fencing Robot — %1 backend").arg(backend_label));
    setMinimumSize(980, 680);
    resize(1280, 820);

    setStyleSheet(R"(
        QMainWindow, QWidget { background: #101217; color: #E8EBF0;
            font-family: "IBM Plex Sans", "Noto Sans"; font-size: 14px; }
        QFrame#sidebar { background: #171A20; border-right: 1px solid #303540; }
        QLabel#brandMark { background: #F0C85A; color: #171A20; border-radius: 8px;
            font-size: 13px; font-weight: 800; padding: 8px; }
        QPushButton#navButton { background: transparent; color: #8F98A8; border: 0;
            border-left: 3px solid transparent; border-radius: 0; padding: 16px 12px;
            text-align: left; font-size: 12px; font-weight: 700; }
        QPushButton#navButton:hover { color: #F4F6F8; background: #1E222A; }
        QPushButton#navButton[active="true"] { color: #F4D374; background: #24231F;
            border-left-color: #F0C85A; }
        QFrame[role="panel"] { background: #171B22; border: 1px solid #303642;
            border-radius: 10px; }
        QLabel[role="sectionLabel"] { color: #8F99AA; font-family: "JetBrains Mono";
            font-size: 11px; font-weight: 700; letter-spacing: 1px; }
        QLabel#pageTitle { color: #F6F7F9; font-size: 25px; font-weight: 750; }
        QLabel#subtle { color: #8E97A6; }
        QLabel#stateBadge { background: #262C35; color: #CFD5DE; border: 1px solid #3A424F;
            border-radius: 7px; padding: 7px 12px; font-family: "JetBrains Mono";
            font-size: 11px; font-weight: 700; }
        QPushButton { background: #232832; color: #E5E9EF; border: 1px solid #3A424F;
            border-radius: 7px; padding: 9px 14px; font-weight: 650; }
        QPushButton:hover { background: #2A303B; border-color: #596475; }
        QPushButton:pressed { background: #1D2129; }
        QPushButton:focus { border: 2px solid #F0C85A; }
        QPushButton[role="primary"] { background: #F0C85A; color: #171A20;
            border-color: #F0C85A; }
        QPushButton[role="primary"]:hover { background: #F6D778; }
        QPushButton[role="danger"] { background: #3A2024; color: #F2B5BC;
            border-color: #704149; font-size: 12px; padding-left: 8px; padding-right: 8px; }
        QPushButton[role="motionCard"] { min-width: 128px; min-height: 58px;
            background: #26241F; color: #F3E7C4; border-color: #5F5335;
            text-align: left; padding-left: 16px; }
        QPushButton[role="motionCard"]:hover { border-color: #D8B45B; background: #302C22; }
        QPushButton[role="positionCard"] { background: #162B39; color: #C9E8F8;
            border-color: #315C74; text-align: left; }
        QPushButton[role="positionCard"]:hover { border-color: #4BA3D9; }
        QLineEdit, QComboBox, QDoubleSpinBox { background: #11151B; color: #EDF0F4;
            border: 1px solid #3A414E; border-radius: 7px; padding: 9px 11px;
            min-height: 20px; selection-background-color: #6B592D; }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border: 2px solid #F0C85A; }
        QComboBox QAbstractItemView { background: #1B1F27; color: #E8EBF0;
            selection-background-color: #343A46; border: 1px solid #444C59; }
        QScrollArea { border: 0; background: transparent; }
        QScrollBar:horizontal { background: #14181E; height: 12px; }
        QScrollBar::handle:horizontal { background: #46505E; border-radius: 5px; min-width: 42px; }
        QScrollBar:vertical { background: #14181E; width: 12px; }
        QScrollBar::handle:vertical { background: #46505E; border-radius: 5px; min-height: 42px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
    )");

    auto* root = new QWidget(this);
    auto* root_layout = new QHBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);
    setCentralWidget(root);

    auto* sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(176);
    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(14, 18, 14, 18);
    sidebar_layout->setSpacing(8);

    auto* brand_mark = new QLabel("FENCING ROBOT");
    brand_mark->setObjectName("brandMark");
    brand_mark->setAlignment(Qt::AlignCenter);
    brand_mark->setFixedHeight(52);
    sidebar_layout->addWidget(brand_mark);
    sidebar_layout->addSpacing(18);

    const QStringList nav_labels{"DRILLS", "REPOSITION", "MONITOR", "CONFIGURE"};
    for (int index = 0; index < nav_labels.size(); ++index) {
        auto* button = new QPushButton(nav_labels[index]);
        button->setObjectName("navButton");
        button->setProperty("active", index == 0);
        button->setMinimumHeight(62);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(nav_labels[index]);
        connect(button, &QPushButton::clicked, this,
                [this, index] { selectPage(index); });
        navigation_buttons_.push_back(button);
        sidebar_layout->addWidget(button);
    }
    sidebar_layout->addStretch();
    state_badge_ = new QLabel("IDLE");
    state_badge_->setObjectName("stateBadge");
    state_badge_->setAlignment(Qt::AlignCenter);
    sidebar_layout->addWidget(state_badge_);
    auto* emergency = actionButton("EMERGENCY STOP", "danger");
    emergency->setMinimumHeight(52);
    sidebar_layout->addWidget(emergency);
    root_layout->addWidget(sidebar);

    pages_ = new QStackedWidget;
    root_layout->addWidget(pages_, 1);

    // Drill editor page -----------------------------------------------------
    auto* drill_page = new QWidget;
    auto* drill_layout = new QVBoxLayout(drill_page);
    drill_layout->setContentsMargins(24, 16, 24, 16);
    drill_layout->setSpacing(10);

    auto* title_row = new QHBoxLayout;
    auto* title_stack = new QVBoxLayout;
    auto* title = new QLabel("Drill composer");
    title->setObjectName("pageTitle");
    title_stack->addWidget(title);
    title_row->addLayout(title_stack, 1);
    title_row->addWidget(sectionLabel("BACKEND"));
    auto* backend_badge = new QLabel(backend_label.toUpper());
    backend_badge->setObjectName("stateBadge");
    title_row->addWidget(backend_badge);
    drill_layout->addLayout(title_row);

    auto* document_bar = new QHBoxLayout;
    drill_name_ = new QLineEdit("Untitled drill");
    drill_name_->setPlaceholderText("Drill name");
    drill_name_->setAccessibleName("Drill name");
    saved_drills_ = new QComboBox;
    saved_drills_->setMinimumWidth(220);
    saved_drills_->setAccessibleName("Saved drills");
    auto* load_button = actionButton("Load");
    auto* new_button = actionButton("New");
    auto* save_button = actionButton("Save drill", "primary");
    document_bar->addWidget(drill_name_, 1);
    document_bar->addWidget(saved_drills_);
    document_bar->addWidget(load_button);
    document_bar->addWidget(new_button);
    document_bar->addWidget(save_button);
    drill_layout->addLayout(document_bar);

    auto* palette = new QFrame;
    palette->setProperty("role", "panel");
    palette->setMaximumHeight(250);
    auto* palette_layout = new QHBoxLayout(palette);
    palette_layout->setContentsMargins(16, 14, 16, 14);
    palette_layout->setSpacing(18);

    auto* motion_library = new QVBoxLayout;
    motion_library->addWidget(sectionLabel("BLADE MOTIONS · CLICK OR DRAG"));
    auto* motion_grid = new QGridLayout;
    motion_grid->setSpacing(8);
    int movement_index = 0;
    for (const auto& movement : movements_) {
        const QString id = QString::fromStdString(movement.id);
        const QString name = QString::fromStdString(movement.display_name);
        auto* card = new PaletteButton(name, [id] { return "motion|" + id; });
        card->setProperty("role", "motionCard");
        card->setToolTip(QString("%1 seconds · drag to schedule")
                             .arg(movement.duration_seconds, 0, 'f', 1));
        card->setAccessibleDescription(card->toolTip());
        connect(card, &QPushButton::clicked, this,
                [this, id] { addMotionStep(id, nextMotionStart()); });
        motion_grid->addWidget(card, movement_index / 3, movement_index % 3);
        ++movement_index;
    }
    motion_library->addLayout(motion_grid);
    motion_library->addStretch();
    palette_layout->addLayout(motion_library, 3);

    auto* positioning_library = new QVBoxLayout;
    positioning_library->addWidget(sectionLabel("POSITIONING · CLICK OR DRAG"));
    const auto add_position_card = [this, positioning_library](const QString& name,
                                                               double direction) {
        auto* frame = new QFrame;
        frame->setProperty("role", "panel");
        auto* layout = new QGridLayout(frame);
        layout->setContentsMargins(11, 10, 11, 10);
        auto* speed = new QDoubleSpinBox;
        speed->setRange(0.01, 0.50);
        speed->setValue(0.15);
        speed->setSingleStep(0.01);
        speed->setDecimals(2);
        speed->setSuffix(" m/s");
        speed->setAccessibleName(name + " speed");
        auto* drag_button = new PaletteButton(
            name, [speed, direction] {
                return QString("positioning|%1")
                    .arg(direction * speed->value(), 0, 'f', 3);
            });
        drag_button->setProperty("role", "positionCard");
        drag_button->setToolTip(
            "Drag to schedule, then drag either end of the timeline block to set distance");
        drag_button->setAccessibleDescription(drag_button->toolTip());
        connect(drag_button, &QPushButton::clicked, this,
                [this, speed, direction] {
                    addPositioningStep(direction * speed->value(),
                                       kDefaultPositioningDurationSeconds,
                                       nextPositioningStart());
                });
        layout->addWidget(drag_button, 0, 0);
        layout->addWidget(speed, 0, 1);
        positioning_library->addWidget(frame);
    };
    add_position_card("Forward", 1.0);
    add_position_card("Backward", -1.0);
    palette_layout->addLayout(positioning_library, 2);
    drill_layout->addWidget(palette);

    auto* timeline_header = new QHBoxLayout;
    timeline_header->addWidget(
        sectionLabel("DRILL TIMELINE · DRAG TO MOVE · POSITIONING ENDS RESIZE"));
    timeline_header->addStretch();
    timeline_summary_ = new QLabel;
    timeline_summary_->setObjectName("subtle");
    timeline_header->addWidget(timeline_summary_);
    drill_layout->addLayout(timeline_header);

    timeline_ = new DrillTimelineWidget(
        [this](const std::string& movement_id) { return movementDuration(movement_id); });
    timeline_->setDropHandler(
        [this](bool positioning, const QString& id, double speed, double duration,
               double start) {
            return positioning ? addPositioningStep(speed, duration, start)
                               : addMotionStep(id, start);
        });
    timeline_->setRemoveHandler(
        [this](bool positioning, std::size_t index) {
            removeTimelineStep(positioning, index);
        });
    timeline_->setEditHandler(
        [this](bool positioning, std::size_t index, double start, double duration) {
            return editTimelineStep(positioning, index, start, duration);
        });
    auto* timeline_scroll = new QScrollArea;
    timeline_scroll->setWidget(timeline_);
    timeline_scroll->setWidgetResizable(false);
    timeline_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timeline_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timeline_scroll->setMinimumHeight(286);
    drill_layout->addWidget(timeline_scroll, 1);

    auto* run_bar = new QHBoxLayout;
    auto* start_button = actionButton("Start selected drill", "primary");
    auto* pause_button = actionButton("Pause");
    auto* resume_button = actionButton("Resume");
    auto* restart_button = actionButton("Restart");
    auto* cancel_button = actionButton("Cancel", "danger");
    run_bar->addWidget(start_button);
    run_bar->addWidget(pause_button);
    run_bar->addWidget(resume_button);
    run_bar->addWidget(restart_button);
    run_bar->addStretch();
    run_bar->addWidget(cancel_button);
    drill_layout->addLayout(run_bar);
    pages_->addWidget(drill_page);

    // Manual repositioning page -------------------------------------------
    auto* reposition_page = new QWidget;
    auto* reposition_layout = new QVBoxLayout(reposition_page);
    reposition_layout->setContentsMargins(28, 24, 28, 24);
    auto* reposition_title = new QLabel("Manual repositioning");
    reposition_title->setObjectName("pageTitle");
    reposition_layout->addWidget(reposition_title);
    auto* reposition_help = new QLabel(
        "Manual movement is available only while no drill is running.\n"
        "These controls remain simulation-only until hardware limits and a dead-man control exist.");
    reposition_help->setObjectName("subtle");
    reposition_layout->addWidget(reposition_help);
    auto* reposition_panel = new QFrame;
    reposition_panel->setProperty("role", "panel");
    auto* reposition_form = new QFormLayout(reposition_panel);
    auto* distance = new QDoubleSpinBox;
    distance->setRange(-1.0, 1.0);
    distance->setDecimals(2);
    distance->setSingleStep(0.05);
    distance->setSuffix(" m");
    auto* move_axial = actionButton("Move axial", "primary");
    auto* rotation = new QDoubleSpinBox;
    rotation->setRange(-360.0, 360.0);
    rotation->setDecimals(1);
    rotation->setSuffix("°");
    auto* rotate = actionButton("Rotate base");
    reposition_form->addRow("Axial distance", distance);
    reposition_form->addRow(QString{}, move_axial);
    reposition_form->addRow("Rotation", rotation);
    reposition_form->addRow(QString{}, rotate);
    reposition_layout->addWidget(reposition_panel);
    reposition_layout->addStretch();
    pages_->addWidget(reposition_page);

    // Monitoring page ------------------------------------------------------
    auto* monitoring_page = new QWidget;
    auto* monitoring_layout = new QVBoxLayout(monitoring_page);
    monitoring_layout->setContentsMargins(28, 24, 28, 24);
    auto* monitoring_title = new QLabel("Robot monitor");
    monitoring_title->setObjectName("pageTitle");
    monitoring_layout->addWidget(monitoring_title);
    auto* status_panel = new QFrame;
    status_panel->setProperty("role", "panel");
    auto* status_layout = new QVBoxLayout(status_panel);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_layout->addWidget(status_);
    auto* status_button = actionButton("Refresh status");
    status_layout->addWidget(status_button, 0, Qt::AlignLeft);
    monitoring_layout->addWidget(status_panel);
    monitoring_layout->addStretch();
    pages_->addWidget(monitoring_page);

    // Configuration page ---------------------------------------------------
    auto* configuration_page = new QWidget;
    auto* configuration_layout = new QVBoxLayout(configuration_page);
    configuration_layout->setContentsMargins(28, 24, 28, 24);
    auto* configuration_title = new QLabel("Robot configuration");
    configuration_title->setObjectName("pageTitle");
    configuration_layout->addWidget(configuration_title);
    auto* configuration_panel = new QFrame;
    configuration_panel->setProperty("role", "panel");
    auto* configuration_form = new QFormLayout(configuration_panel);
    handedness_ = new QComboBox;
    handedness_->addItems({"Select handedness", "Left", "Right"});
    auto* apply_handedness = actionButton("Apply handedness", "primary");
    auto* calibrate = actionButton("Simulate calibration");
    configuration_form->addRow("Robot handedness", handedness_);
    configuration_form->addRow(QString{}, apply_handedness);
    configuration_form->addRow("Motor ranges", calibrate);
    configuration_layout->addWidget(configuration_panel);
    auto* configuration_note = new QLabel(
        "Physical handedness validation and stored per-motor calibration are not implemented.");
    configuration_note->setObjectName("subtle");
    configuration_note->setWordWrap(true);
    configuration_layout->addWidget(configuration_note);
    configuration_layout->addStretch();
    pages_->addWidget(configuration_page);

    connect(new_button, &QPushButton::clicked, this, [this] { clearDraft(); });
    connect(load_button, &QPushButton::clicked, this, [this] { loadSelectedDrill(); });
    connect(save_button, &QPushButton::clicked, this, [this] { saveDrill(); });
    connect(start_button, &QPushButton::clicked, this, [this] {
        if (saved_drills_->currentIndex() < 0 ||
            !control_.startDrill(saved_drills_->currentData().toString().toStdString())) {
            showFailure("Start drill");
        }
        refreshStatus();
    });
    connect(pause_button, &QPushButton::clicked, this, [this] {
        if (!control_.pauseDrill()) showFailure("Pause drill");
        refreshStatus();
    });
    connect(resume_button, &QPushButton::clicked, this, [this] {
        if (!control_.resumeDrill()) showFailure("Resume drill");
        refreshStatus();
    });
    connect(restart_button, &QPushButton::clicked, this, [this] {
        if (!control_.restartDrill()) showFailure("Restart drill");
        refreshStatus();
    });
    connect(cancel_button, &QPushButton::clicked, this, [this] {
        if (!control_.cancelDrill()) showFailure("Cancel drill");
        refreshStatus();
    });
    connect(emergency, &QPushButton::clicked, this, [this] {
        const auto response = QMessageBox::warning(
            this, "Emergency stop",
            "Latch the software emergency-stop state? This demonstration is not a safety-rated stop.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (response == QMessageBox::Yes) control_.requestEmergencyStop();
        refreshStatus();
    });
    connect(move_axial, &QPushButton::clicked, this, [this, distance] {
        if (!control_.requestManualAxialMovement(distance->value())) {
            showFailure("Move axial");
        }
        refreshStatus();
    });
    connect(rotate, &QPushButton::clicked, this, [this, rotation] {
        if (!control_.requestManualRotation(rotation->value())) showFailure("Rotate base");
        refreshStatus();
    });
    connect(apply_handedness, &QPushButton::clicked, this, [this] {
        auto hand = domain::Handedness::Unknown;
        if (handedness_->currentIndex() == 1) hand = domain::Handedness::Left;
        if (handedness_->currentIndex() == 2) hand = domain::Handedness::Right;
        if (!configuring_.configureHandedness(hand)) showFailure("Configure handedness");
        refreshStatus();
    });
    connect(calibrate, &QPushButton::clicked, this, [this] {
        if (!configuring_.calibrateRobot()) showFailure("Calibrate");
        refreshStatus();
    });
    connect(status_button, &QPushButton::clicked, this, [this] { refreshStatus(); });

    refreshSavedDrills();
    if (saved_drills_->count() > 0) loadSelectedDrill();
    else clearDraft();
    refreshStatus();

    auto* status_refresh = new QTimer(this);
    connect(status_refresh, &QTimer::timeout, this, [this] { refreshStatus(); });
    status_refresh->start(250);
}

double MainWindow::movementDuration(const std::string& movement_id) const {
    const auto movement = std::ranges::find_if(movements_, [&movement_id](const auto& candidate) {
        return candidate.id == movement_id;
    });
    return movement == movements_.end() ? 0.0 : movement->duration_seconds;
}

bool MainWindow::addMotionStep(const QString& movement_id, double start_time_seconds) {
    const std::string id = movement_id.toStdString();
    const double duration = movementDuration(id);
    if (duration <= 0.0) {
        QMessageBox::warning(this, "Cannot schedule motion",
                             "This movement does not have a positive duration.");
        return false;
    }

    const double end = start_time_seconds + duration;
    for (const auto& other : draft_.motion_steps) {
        const double other_end = other.start_time_seconds + movementDuration(other.movement_id);
        if (start_time_seconds < other_end && other.start_time_seconds < end) {
            QMessageBox::warning(this, "Motion overlap",
                                 "Blade motions cannot overlap. Choose another start time.");
            return false;
        }
    }

    draft_.motion_steps.push_back({id, start_time_seconds});
    std::ranges::sort(draft_.motion_steps, {}, &domain::MotionStep::start_time_seconds);
    refreshEditor();
    return true;
}

bool MainWindow::addPositioningStep(double speed_mps, double duration_seconds,
                                    double start_time_seconds) {
    if (speed_mps == 0.0 || duration_seconds <= 0.0) {
        QMessageBox::warning(this, "Cannot schedule positioning",
                             "Positioning needs a non-zero speed and positive duration.");
        return false;
    }

    const double end = start_time_seconds + duration_seconds;
    for (const auto& other : draft_.positioning_steps) {
        const double other_end = other.start_time_seconds + other.duration_seconds;
        if (start_time_seconds < other_end && other.start_time_seconds < end) {
            QMessageBox::warning(this, "Positioning overlap",
                                 "Positioning steps cannot overlap. Choose another start time.");
            return false;
        }
    }

    draft_.positioning_steps.push_back(
        {.start_time_seconds = start_time_seconds,
         .speed_mps = speed_mps,
         .duration_seconds = duration_seconds});
    std::ranges::sort(draft_.positioning_steps, {},
                      &domain::PositioningStep::start_time_seconds);
    refreshEditor();
    return true;
}

bool MainWindow::editTimelineStep(bool positioning, std::size_t index,
                                  double start_time_seconds,
                                  double duration_seconds) {
    if (start_time_seconds < 0.0) return false;

    if (positioning) {
        if (index >= draft_.positioning_steps.size() ||
            duration_seconds < kMinimumPositioningDurationSeconds) {
            return false;
        }

        const double end = start_time_seconds + duration_seconds;
        for (std::size_t other_index = 0;
             other_index < draft_.positioning_steps.size(); ++other_index) {
            if (other_index == index) continue;
            const auto& other = draft_.positioning_steps[other_index];
            const double other_end = other.start_time_seconds + other.duration_seconds;
            if (start_time_seconds < other_end && other.start_time_seconds < end) {
                QMessageBox::warning(
                    this, "Positioning overlap",
                    "Positioning steps cannot overlap. The previous timing was kept.");
                return false;
            }
        }

        auto& step = draft_.positioning_steps[index];
        step.start_time_seconds = start_time_seconds;
        step.duration_seconds = duration_seconds;
        std::ranges::sort(draft_.positioning_steps, {},
                          &domain::PositioningStep::start_time_seconds);
    } else {
        if (index >= draft_.motion_steps.size()) return false;
        const double duration = movementDuration(draft_.motion_steps[index].movement_id);
        const double end = start_time_seconds + duration;
        for (std::size_t other_index = 0;
             other_index < draft_.motion_steps.size(); ++other_index) {
            if (other_index == index) continue;
            const auto& other = draft_.motion_steps[other_index];
            const double other_end =
                other.start_time_seconds + movementDuration(other.movement_id);
            if (start_time_seconds < other_end && other.start_time_seconds < end) {
                QMessageBox::warning(
                    this, "Motion overlap",
                    "Blade motions cannot overlap. The previous timing was kept.");
                return false;
            }
        }

        draft_.motion_steps[index].start_time_seconds = start_time_seconds;
        std::ranges::sort(draft_.motion_steps, {},
                          &domain::MotionStep::start_time_seconds);
    }

    refreshEditor();
    return true;
}

void MainWindow::removeTimelineStep(bool positioning, std::size_t index) {
    if (positioning) {
        if (index < draft_.positioning_steps.size()) {
            draft_.positioning_steps.erase(draft_.positioning_steps.begin() + index);
        }
    } else if (index < draft_.motion_steps.size()) {
        draft_.motion_steps.erase(draft_.motion_steps.begin() + index);
    }
    refreshEditor();
}

double MainWindow::nextMotionStart() const {
    double next = 0.0;
    for (const auto& step : draft_.motion_steps) {
        next = std::max(next, step.start_time_seconds + movementDuration(step.movement_id));
    }
    return std::ceil(next / kTimelineSnapSeconds) * kTimelineSnapSeconds;
}

double MainWindow::nextPositioningStart() const {
    double next = 0.0;
    for (const auto& step : draft_.positioning_steps) {
        next = std::max(next, step.start_time_seconds + step.duration_seconds);
    }
    return std::ceil(next / kTimelineSnapSeconds) * kTimelineSnapSeconds;
}

void MainWindow::clearDraft() {
    draft_ = {};
    drill_name_->setText("Untitled drill");
    refreshEditor();
}

void MainWindow::loadSelectedDrill() {
    const std::string id = saved_drills_->currentData().toString().toStdString();
    const auto drills = drills_.listDrills();
    const auto drill = std::ranges::find_if(drills, [&id](const auto& candidate) {
        return candidate.id == id;
    });
    if (drill == drills.end()) {
        showFailure("Load drill");
        return;
    }
    draft_ = *drill;
    drill_name_->setText(QString::fromStdString(draft_.name));
    refreshEditor();
}

void MainWindow::saveDrill() {
    const QString name = drill_name_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Save drill", "Enter a drill name before saving.");
        drill_name_->setFocus();
        return;
    }

    draft_.name = name.toStdString();
    if (draft_.id.empty()) {
        QString slug = name.toLower();
        slug.replace(QRegularExpression("[^a-z0-9]+"), "_");
        slug = slug.trimmed();
        if (slug.isEmpty()) slug = "drill";
        draft_.id = QString("%1_%2")
                        .arg(slug)
                        .arg(QDateTime::currentMSecsSinceEpoch())
                        .toStdString();
    }

    if (!drills_.saveDrill(draft_)) {
        showFailure("Save drill");
        return;
    }
    const QString saved_id = QString::fromStdString(draft_.id);
    refreshSavedDrills();
    const int index = saved_drills_->findData(saved_id);
    if (index >= 0) saved_drills_->setCurrentIndex(index);
}

void MainWindow::refreshEditor() {
    timeline_->setDraft(&draft_);
    double duration = 0.0;
    for (const auto& step : draft_.motion_steps) {
        duration = std::max(duration,
                            step.start_time_seconds + movementDuration(step.movement_id));
    }
    for (const auto& step : draft_.positioning_steps) {
        duration = std::max(duration,
                            step.start_time_seconds + step.duration_seconds);
    }
    timeline_summary_->setText(
        QString("%1 motions  ·  %2 positionings  ·  %3s total")
            .arg(draft_.motion_steps.size())
            .arg(draft_.positioning_steps.size())
            .arg(duration, 0, 'f', 1));
}

void MainWindow::refreshSavedDrills() {
    const QVariant selected = saved_drills_->currentData();
    saved_drills_->clear();
    for (const auto& drill : drills_.listDrills()) {
        saved_drills_->addItem(QString::fromStdString(drill.name),
                               QString::fromStdString(drill.id));
    }
    const int previous = saved_drills_->findData(selected);
    if (previous >= 0) saved_drills_->setCurrentIndex(previous);
}

void MainWindow::refreshStatus() {
    const auto status = monitoring_.checkRobotStatus();
    const QString hand = status.configuration.handedness == domain::Handedness::Left
                             ? "Left"
                         : status.configuration.handedness == domain::Handedness::Right
                             ? "Right"
                             : "Unknown";
    state_badge_->setText(drillStateLabel(status.drill_state));
    status_->setText(
        QString("DRILL STATE\n%1\n\nROBOT CONFIGURATION\nHandedness: %2\n"
                "Calibration: %3\n\nPOWER\n%4% available (simulated)\n\n"
                "HARDWARE HEALTH\n%5 (simulated)\n\nLAST MESSAGE\n%6")
            .arg(domain::toString(status.drill_state))
            .arg(hand)
            .arg(status.configuration.calibrated ? "Complete (simulated)" : "Required")
            .arg(status.power.charge_percent, 0, 'f', 1)
            .arg(status.health.healthy ? "Healthy" : "Fault")
            .arg(QString::fromStdString(status.last_error.empty() ? "None" : status.last_error)));
}

void MainWindow::selectPage(int index) {
    if (index < 0 || index >= pages_->count()) return;
    pages_->setCurrentIndex(index);
    for (int button_index = 0;
         button_index < static_cast<int>(navigation_buttons_.size()); ++button_index) {
        auto* button = navigation_buttons_[button_index];
        button->setProperty("active", button_index == index);
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void MainWindow::showFailure(const QString& action) {
    const auto state = monitoring_.checkRobotStatus();
    QMessageBox::warning(
        this, action,
        QString::fromStdString(state.last_error.empty() ? "Operation was not accepted"
                                                        : state.last_error));
}

} // namespace fencing::ui
