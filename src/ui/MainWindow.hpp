#pragma once
#include "core/ports/driving/ForConfiguringRobot.hpp"
#include "core/ports/driving/ForManagingDrills.hpp"
#include "core/ports/driving/ForMonitoringRobot.hpp"
#include "core/ports/driving/ForDrillOperation.hpp"
#include <QMainWindow>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace fencing::ui {
// Qt is only in this DRIVING ADAPTER. Not a single Qt header in src/core.
class MainWindow final : public QMainWindow {
public:
    MainWindow(ports::driving::ForManagingDrills& drills,
               ports::driving::ForDrillOperation& control,
               ports::driving::ForConfiguringRobot& configuring,
               ports::driving::ForMonitoringRobot& monitoring,
               QString backend_label,
               QWidget* parent = nullptr);
private:
    void addStep();
    void saveDrill();
    void refreshStatus();
    void refreshSavedDrills();
    void showFailure(const QString& action);

    ports::driving::ForManagingDrills& drills_;
    ports::driving::ForDrillOperation& control_;
    ports::driving::ForConfiguringRobot& configuring_;
    ports::driving::ForMonitoringRobot& monitoring_;
    QComboBox* movement_picker_{};
    QDoubleSpinBox* axial_speed_{};
    QComboBox* saved_drills_{};
    QComboBox* handedness_{};
    QLineEdit* drill_name_{};
    QListWidget* steps_{};
    QLabel* status_{};
    domain::Drill draft_;
};
} // namespace fencing::ui
