#include "ui/MainWindow.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <utility>

namespace fencing::ui {
namespace {
QPushButton* makeButton(const QString& text, QLayout* layout) {
    auto* button = new QPushButton(text);
    button->setMinimumHeight(40);  // larger touch targets
    layout->addWidget(button);
    return button;
}
} // namespace

/*
    UI Requirements
        
*/
MainWindow::MainWindow(ports::driving::ForManagingDrills& drills,
                       ports::driving::ForDrillOperation& control,
                       ports::driving::ForConfiguringRobot& configuring,
                       ports::driving::ForMonitoringRobot& monitoring,
                       QString backend_label, QWidget* parent)
    : QMainWindow(parent), drills_(drills), control_(control),
      configuring_(configuring), monitoring_(monitoring) {
    setWindowTitle(QString("Fencing Robot — %1 backend (starter)").arg(backend_label));
    resize(760, 550);
    auto* tabs = new QTabWidget(this);
    setCentralWidget(tabs);

    auto* drill_tab = new QWidget;
    auto* drill_layout = new QVBoxLayout(drill_tab);
    drill_layout->addWidget(new QLabel(
        "Outline UI. Simulation is the ONLY functioning motion backend.\n"
        "No real motor, physical E-stop, or persistence implementation is present."));

    auto* builder = new QGroupBox("Build a drill (ordered blade moves + optional axial overlay)");
    auto* builder_layout = new QVBoxLayout(builder);
    drill_name_ = new QLineEdit("My drill");
    builder_layout->addWidget(drill_name_);
    auto* add_row = new QHBoxLayout;
    movement_picker_ = new QComboBox;
    for (const auto& id : {"cut_3", "cut_4", "cut_5", "parry_3", "parry_4", "parry_5"})
        movement_picker_->addItem(id);
    axial_speed_ = new QDoubleSpinBox;
    axial_speed_->setRange(-0.2, 0.2); // UI-only DEMO range, not certified limits!
    axial_speed_->setDecimals(2);
    axial_speed_->setSingleStep(0.05);
    axial_speed_->setSuffix(" m/s axial");
    add_row->addWidget(movement_picker_);
    add_row->addWidget(axial_speed_);
    auto* add_button = makeButton("Add step", add_row);
    builder_layout->addLayout(add_row);
    steps_ = new QListWidget;
    builder_layout->addWidget(steps_);
    auto* save_button = makeButton("Save draft (memory only)", builder_layout);
    drill_layout->addWidget(builder);

    auto* run_group = new QGroupBox("Saved drills");
    auto* run_layout = new QVBoxLayout(run_group);
    saved_drills_ = new QComboBox;
    run_layout->addWidget(saved_drills_);
    auto* controls = new QHBoxLayout;
    auto* start_button = makeButton("Start", controls);
    auto* pause_button = makeButton("Pause", controls);
    auto* resume_button = makeButton("Resume", controls);
    auto* restart_button = makeButton("Restart", controls);
    auto* cancel_button = makeButton("Cancel", controls);
    run_layout->addLayout(controls);
    drill_layout->addWidget(run_group);
    tabs->addTab(drill_tab, "Drills");

    auto* configuration_tab = new QWidget;
    auto* configuration_layout = new QVBoxLayout(configuration_tab);
    auto* configuration_form = new QFormLayout;
    handedness_ = new QComboBox;
    handedness_->addItems({"Select handedness", "Left", "Right"});
    configuration_form->addRow("Robot handedness", handedness_);
    configuration_layout->addLayout(configuration_form);
    auto* apply_handedness = makeButton("Apply handedness", configuration_layout);
    auto* calibrate = makeButton("Simulate calibration", configuration_layout);
    configuration_layout->addWidget(new QLabel(
        "TODO: tool-free physical handedness validation and stored per-motor calibration."));
    configuration_layout->addStretch();
    tabs->addTab(configuration_tab, "Configuration");

    auto* monitoring_tab = new QWidget;
    auto* monitoring_layout = new QVBoxLayout(monitoring_tab);
    status_ = new QLabel;
    status_->setWordWrap(true);
    monitoring_layout->addWidget(status_);
    auto* status_button = makeButton("Refresh status", monitoring_layout);
    monitoring_layout->addStretch();
    tabs->addTab(monitoring_tab, "Monitoring");

    connect(add_button, &QPushButton::clicked, this, [this] { addStep(); });
    connect(save_button, &QPushButton::clicked, this, [this] { saveDrill(); });
    connect(start_button, &QPushButton::clicked, this, [this] {
        if (!control_.startDrill(saved_drills_->currentData().toString().toStdString()))
            showFailure("Start drill");
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
    refreshStatus();
    auto* status_refresh = new QTimer(this);
    connect(status_refresh, &QTimer::timeout, this, [this] { refreshStatus(); });
    status_refresh->start(250); // view refresh only; hardware timing is separate
}

void MainWindow::addStep() {
    domain::DrillStep step;
    step.movement_id = movement_picker_->currentText().toStdString();
    if (axial_speed_->value() != 0.0)
        step.axial = domain::AxialProfile{axial_speed_->value(), 0.5};
    draft_.steps.push_back(step);
    steps_->addItem(QString::fromStdString(step.movement_id) +
                    (step.axial ? QString(" | axial %1 m/s (demo)").arg(step.axial->speed_mps) : ""));
}
void MainWindow::saveDrill() {
    draft_.id = "user_draft";  // TODO: stable UUID, editing and versioning
    draft_.name = drill_name_->text().toStdString();
    if (!drills_.saveDrill(draft_)) { showFailure("Save drill"); return; }
    draft_ = {};
    steps_->clear();
    refreshSavedDrills();
}
void MainWindow::refreshSavedDrills() {
    saved_drills_->clear();
    for (const auto& drill : drills_.listDrills())
        saved_drills_->addItem(QString::fromStdString(drill.name), QString::fromStdString(drill.id));
}
void MainWindow::refreshStatus() {
    const auto status = monitoring_.checkRobotStatus();
    const QString hand = status.configuration.handedness == domain::Handedness::Left ? "Left" :
        status.configuration.handedness == domain::Handedness::Right ? "Right" : "Unknown";
    status_->setText(QString("Drill state: %1\nHandedness: %2\nCalibrated (demo): %3\n"
                             "Power: %4% (SIMULATED)\nHardware health: %5 (SIMULATED)\n"
                             "Error: %6")
                         .arg(domain::toString(status.drill_state))
                         .arg(hand)
                         .arg(status.configuration.calibrated ? "Yes" : "No")
                         .arg(status.power.charge_percent)
                         .arg(status.health.healthy ? "Healthy" : "Fault")
                         .arg(QString::fromStdString(status.last_error)));
}
void MainWindow::showFailure(const QString& action) {
    const auto state = monitoring_.checkRobotStatus();
    QMessageBox::warning(this, action, QString::fromStdString(
        state.last_error.empty() ? "Operation was not accepted" : state.last_error));
}
} // namespace fencing::ui
