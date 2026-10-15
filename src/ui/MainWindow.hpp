#pragma once

#include "core/ports/driving/ForConfiguringRobot.hpp"
#include "core/ports/driving/ForDrillOperation.hpp"
#include "core/ports/driving/ForManagingDrills.hpp"
#include "core/ports/driving/ForMonitoringRobot.hpp"
#include <QMainWindow>
#include <QString>
#include <cstddef>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace fencing::ui {

class DrillTimelineWidget;

// Qt is only in this DRIVING ADAPTER. No Qt types cross into src/core.
class MainWindow final : public QMainWindow {
public:
    MainWindow(ports::driving::ForManagingDrills& drills,
               ports::driving::ForDrillOperation& control,
               ports::driving::ForConfiguringRobot& configuring,
               ports::driving::ForMonitoringRobot& monitoring,
               QString backend_label,
               QWidget* parent = nullptr);

private:
    bool addMotionStep(const QString& movement_id, double start_time_seconds);
    bool addPositioningStep(double speed_mps, double duration_seconds,
                            double start_time_seconds);
    bool editTimelineStep(bool positioning, std::size_t index,
                          double start_time_seconds, double duration_seconds);
    void removeTimelineStep(bool positioning, std::size_t index);
    void clearDraft();
    void loadSelectedDrill();
    void saveDrill();
    void refreshEditor();
    void refreshStatus();
    void refreshSavedDrills();
    void selectPage(int index);
    void showFailure(const QString& action);
    double movementDuration(const std::string& movement_id) const;
    double nextMotionStart() const;
    double nextPositioningStart() const;

    ports::driving::ForManagingDrills& drills_;
    ports::driving::ForDrillOperation& control_;
    ports::driving::ForConfiguringRobot& configuring_;
    ports::driving::ForMonitoringRobot& monitoring_;
    std::vector<domain::MovementDefinition> movements_;
    std::vector<QPushButton*> navigation_buttons_;
    QStackedWidget* pages_{};
    QComboBox* saved_drills_{};
    QComboBox* handedness_{};
    QLineEdit* drill_name_{};
    DrillTimelineWidget* timeline_{};
    QLabel* timeline_summary_{};
    QLabel* state_badge_{};
    QLabel* status_{};
    domain::Drill draft_;
};

} // namespace fencing::ui
