#include "adapters/calibration/PlaceholderCalibrationAdapters.hpp"
#include "adapters/health/PlaceholderHealthAdapter.hpp"
#include "adapters/motion/LeRobotMotionAdapter.hpp"
#include "adapters/motion/SimulatedMotionAdapter.hpp"
#include "adapters/motion/UnimplementedMotionAdapters.hpp"
#include "adapters/persistence/SqliteRobotDataAdapter.hpp"
#include "adapters/power/PlaceholderPowerAdapter.hpp"
#include "adapters/safety/PlaceholderSafePowerAdapter.hpp"
#include "core/application/FencingRobotApplication.hpp"
#include "ui/MainWindow.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    QApplication qt(argc, argv);
    QString backend = "sim";
    QString database_path;
    QString lerobot_root;
    QString lerobot_config;
    QString bridge_script =
        QDir(QCoreApplication::applicationDirPath()).filePath("bridges/lerobot_bridge.py");
    bool enable_hardware = false;
    for (const QString& arg : qt.arguments()) {
        if (arg.startsWith("--backend=")) backend = arg.mid(10);
        else if (arg.startsWith("--database=")) database_path = arg.mid(11);
        else if (arg.startsWith("--lerobot-root=")) lerobot_root = arg.mid(15);
        else if (arg.startsWith("--lerobot-config=")) lerobot_config = arg.mid(17);
        else if (arg.startsWith("--bridge-script=")) bridge_script = arg.mid(16);
        else if (arg == "--enable-hardware") enable_hardware = true;
    }

    if (database_path.isEmpty()) {
        database_path = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation)).filePath("fencing_robot.sqlite3");
    }

    std::unique_ptr<fencing::adapters::SqliteRobotDataAdapter> data;
    try {
        data = std::make_unique<fencing::adapters::SqliteRobotDataAdapter>(
            std::filesystem::path(database_path.toStdString()));
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, "Database error", error.what());
        return 2;
    }

    // COMPOSITION ROOT: the one place that knows concrete adapters.
    std::unique_ptr<fencing::ports::driven::ForExecutingRobotMotion> motion;
    if (backend == "sim") motion = std::make_unique<fencing::adapters::SimulatedMotionAdapter>();
    else if (backend == "lerobot") {
        if (lerobot_root.isEmpty() || lerobot_config.isEmpty()) {
            QMessageBox::critical(
                nullptr, "Missing LeRobot configuration",
                "The LeRobot backend requires --lerobot-root=<path> and "
                "--lerobot-config=<path>.");
            return 2;
        }
        fencing::adapters::LeRobotProcessConfig process_config{
            .program = "uv",
            .arguments = {"run", "--project", lerobot_root.toStdString(), "python",
                          bridge_script.toStdString(), "--config",
                          lerobot_config.toStdString()},
        };
        if (enable_hardware) process_config.arguments.push_back("--enable-hardware");
        auto adapter = std::make_unique<fencing::adapters::LeRobotMotionAdapter>(
            std::move(process_config));
        if (!adapter->isReady()) {
            QMessageBox::critical(nullptr, "LeRobot bridge error",
                                  QString::fromStdString(adapter->lastError()));
            return 2;
        }

        auto configuration = data->loadConfiguration();
        configuration.calibrated = adapter->isCalibrated();
        if (!data->saveConfiguration(configuration)) {
            QMessageBox::critical(nullptr, "Database error",
                                  "Unable to store LeRobot calibration status.");
            return 2;
        }
        motion = std::move(adapter);
    }
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
    fencing::adapters::DemoPowerAdapter power;
    fencing::adapters::DemoHealthAdapter health;
    fencing::adapters::DemoSafePowerAdapter safe_power;
    fencing::application::FencingRobotApplication core(
        *motion, calibration, *data, power, health, safe_power);

    fencing::ui::MainWindow window(core, core, core, core, backend);
    window.showMaximized();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&core] { core.tick(); });
    timer.start(25); // UI-process polling; not a motor-control clock.
    return qt.exec();
}
