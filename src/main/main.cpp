#include "adapters/calibration/PlaceholderCalibrationAdapters.hpp"
#include "adapters/health/PlaceholderHealthAdapter.hpp"
#include "adapters/motion/SimulatedMotionAdapter.hpp"
#include "adapters/motion/UnimplementedMotionAdapters.hpp"
#include "adapters/persistence/InMemoryRobotDataAdapter.hpp"
#include "adapters/power/PlaceholderPowerAdapter.hpp"
#include "adapters/safety/PlaceholderSafePowerAdapter.hpp"
#include "core/application/FencingRobotApplication.hpp"
#include "ui/MainWindow.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    QApplication qt(argc, argv);
    QString backend = "sim";
    for (const QString& arg : qt.arguments()) {
        if (arg.startsWith("--backend=")) backend = arg.mid(10);
    }

    // COMPOSITION ROOT: the one place that knows concrete adapters.
    std::unique_ptr<fencing::ports::driven::ForExecutingRobotMotion> motion;
    if (backend == "sim") motion = std::make_unique<fencing::adapters::SimulatedMotionAdapter>();
    else if (backend == "lerobot") motion = std::make_unique<fencing::adapters::LeRobotMotionAdapter>();
    else if (backend == "ros2") motion = std::make_unique<fencing::adapters::ROS2MotionAdapter>();
    else {
        QMessageBox::critical(nullptr, "Unsupported backend", "Use --backend=sim|lerobot|ros2");
        return 2;
    }

    fencing::adapters::SimulatedCalibrationAdapter demo_calibration;
    fencing::adapters::UnimplementedCalibrationAdapter real_calibration;
    auto& calibration = backend == "sim"
        ? static_cast<fencing::ports::driven::ForCalibratingMotionHardware&>(demo_calibration)
        : static_cast<fencing::ports::driven::ForCalibratingMotionHardware&>(real_calibration);
    fencing::adapters::InMemoryRobotDataAdapter data;
    fencing::adapters::DemoPowerAdapter power;
    fencing::adapters::DemoHealthAdapter health;
    fencing::adapters::DemoSafePowerAdapter safe_power;
    fencing::application::FencingRobotApplication core(
        *motion, calibration, data, power, health, safe_power);

    fencing::ui::MainWindow window(core, core, core, core, backend);
    window.showMaximized();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&core] { core.tick(); });
    timer.start(25); // UI-process polling; not a motor-control clock.
    return qt.exec();
}
