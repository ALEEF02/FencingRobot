#include "adapters/motion/LeRobotMotionAdapter.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace fencing::adapters {
namespace {

constexpr int kProtocolVersion = 1;

enum class Channel { None, Motion, Positioning };

domain::ExecutionState parseState(const QString& value) {
    if (value == "idle") return domain::ExecutionState::Idle;
    if (value == "running") return domain::ExecutionState::Running;
    if (value == "succeeded") return domain::ExecutionState::Succeeded;
    return domain::ExecutionState::Failed;
}

QString handednessName(domain::Handedness handedness) {
    if (handedness == domain::Handedness::Left) return "left";
    if (handedness == domain::Handedness::Right) return "right";
    return "unknown";
}

} // namespace

class LeRobotMotionAdapter::Impl {
public:
    explicit Impl(LeRobotProcessConfig config) : config_(std::move(config)) {
        process_.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PYTHONUNBUFFERED", "1");
        process_.setProcessEnvironment(environment);

        QStringList arguments;
        for (const auto& argument : config_.arguments) {
            arguments.push_back(QString::fromStdString(argument));
        }
        process_.start(QString::fromStdString(config_.program), arguments,
                       QIODevice::ReadWrite | QIODevice::Text);
        if (!process_.waitForStarted(clampedMilliseconds(config_.startup_timeout))) {
            setError("Unable to start LeRobot bridge: " + process_.errorString().toStdString());
            return;
        }

        const auto deadline = Clock::now() + config_.startup_timeout;
        while (!ready_ && process_.state() != QProcess::NotRunning &&
               Clock::now() < deadline) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now());
            process_.waitForReadyRead(std::max(1, std::min(50, clampedMilliseconds(remaining))));
            drainMessages();
        }
        if (!ready_ && error_.empty()) setError("LeRobot bridge did not become ready in time");
    }

    ~Impl() {
        if (process_.state() == QProcess::NotRunning) return;
        QJsonObject shutdown{{"op", "shutdown"}};
        writeMessage(shutdown, Channel::None, false);
        process_.waitForBytesWritten(200);
        if (process_.waitForFinished(clampedMilliseconds(config_.shutdown_timeout))) return;
        process_.terminate();
        if (process_.waitForFinished(500)) return;
        process_.kill();
        process_.waitForFinished(500);
    }

    bool startMotion(const domain::MotionPlan& plan) {
        refreshHealth();
        if (!healthy() || motion_state_ == domain::ExecutionState::Running ||
            plan.blade_movement.id.empty() ||
            !std::isfinite(plan.blade_movement.duration_seconds) ||
            plan.blade_movement.duration_seconds <= 0.0 ||
            plan.handedness == domain::Handedness::Unknown) {
            return false;
        }
        QJsonObject request{
            {"op", "start_motion"},
            {"movement_id", QString::fromStdString(plan.blade_movement.id)},
            {"handedness", handednessName(plan.handedness)},
            {"default_duration_s", plan.blade_movement.duration_seconds},
        };
        if (!writeMessage(request, Channel::Motion)) return false;
        motion_state_ = domain::ExecutionState::Running;
        return true;
    }

    bool startPositioning(const domain::PositioningStep& step) {
        refreshHealth();
        if (!healthy() || positioning_state_ == domain::ExecutionState::Running ||
            !std::isfinite(step.speed_mps) || step.speed_mps == 0.0 ||
            !std::isfinite(step.duration_seconds) || step.duration_seconds <= 0.0) {
            return false;
        }
        QJsonObject request{
            {"op", "start_positioning"},
            {"speed_mps", step.speed_mps},
            {"duration_s", step.duration_seconds},
        };
        if (!writeMessage(request, Channel::Positioning)) return false;
        positioning_state_ = domain::ExecutionState::Running;
        return true;
    }

    domain::ExecutionState poll(Channel channel) {
        refreshHealth();
        return channel == Channel::Motion ? motion_state_ : positioning_state_;
    }

    bool simpleCommand(const char* operation) {
        refreshHealth();
        if (!healthy()) return false;
        return writeMessage({{"op", operation}}, Channel::None);
    }

    bool cancel() {
        if (!simpleCommand("cancel")) return false;
        motion_state_ = domain::ExecutionState::Idle;
        positioning_state_ = domain::ExecutionState::Idle;
        return true;
    }

    bool manualAxial(double distance_m) {
        refreshHealth();
        if (!healthy() || !std::isfinite(distance_m) || distance_m == 0.0) return false;
        return writeMessage({{"op", "manual_axial"}, {"distance_m", distance_m}},
                            Channel::None);
    }

    bool ready() const { return ready_; }
    bool calibrated() const { return calibrated_; }
    const std::string& error() const { return error_; }
    void unsupportedRotation() { setError("Manual rotation is not supported by this adapter"); }

private:
    using Clock = std::chrono::steady_clock;

    static int clampedMilliseconds(std::chrono::milliseconds duration) {
        return static_cast<int>(std::clamp<long long>(duration.count(), 0, 2147483647));
    }

    bool healthy() const {
        return ready_ && process_.state() != QProcess::NotRunning;
    }

    void refreshHealth() {
        drainMessages();
        if (process_.state() == QProcess::NotRunning) {
            failActive("LeRobot bridge process exited");
            ready_ = false;
            return;
        }
        if (ready_ && Clock::now() - last_heartbeat_ > config_.heartbeat_timeout) {
            failActive("LeRobot bridge heartbeat timed out");
            ready_ = false;
            process_.terminate();
        }
    }

    bool writeMessage(QJsonObject request, Channel channel,
                      bool require_ready = true) {
        if (require_ready && !healthy()) return false;
        if (process_.state() == QProcess::NotRunning) return false;

        const std::string request_id = std::to_string(next_request_id_++);
        request.insert("protocol", kProtocolVersion);
        request.insert("request_id", QString::fromStdString(request_id));
        const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (process_.write(payload) != payload.size()) {
            setError("Unable to write to LeRobot bridge");
            return false;
        }
        pending_[request_id] = channel;
        return true;
    }

    void drainMessages() {
        // QProcess is polled by the application's existing tick loop, so the
        // adapter does not depend on Qt signals or an event-loop callback.
        process_.waitForReadyRead(0);
        while (process_.canReadLine()) {
            const QByteArray line = process_.readLine().trimmed();
            if (line.isEmpty()) continue;
            QJsonParseError parse_error;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
                failActive("Malformed JSON from LeRobot bridge");
                ready_ = false;
                continue;
            }
            handleMessage(document.object());
        }
    }

    void handleMessage(const QJsonObject& message) {
        if (message.value("protocol").toInt() != kProtocolVersion) {
            failActive("Unsupported LeRobot bridge protocol version");
            ready_ = false;
            return;
        }
        const QString type = message.value("type").toString();
        if (type == "ready") {
            calibrated_ = message.value("calibrated").toBool();
            ready_ = true;
            error_.clear();
            last_heartbeat_ = Clock::now();
            return;
        }
        if (type == "heartbeat") {
            last_heartbeat_ = Clock::now();
            return;
        }
        if (type == "state") {
            const QString channel = message.value("channel").toString();
            const auto state = parseState(message.value("state").toString());
            if (channel == "motion") motion_state_ = state;
            else if (channel == "positioning") positioning_state_ = state;
            if (state == domain::ExecutionState::Failed) {
                setError(message.value("error").toString("LeRobot operation failed").toStdString());
            }
            return;
        }
        if (type != "response") return;

        const std::string id = message.value("request_id").toString().toStdString();
        const auto pending = pending_.find(id);
        const Channel channel = pending == pending_.end() ? Channel::None : pending->second;
        if (pending != pending_.end()) pending_.erase(pending);
        if (message.value("accepted").toBool()) return;

        const std::string reason =
            message.value("error").toString("LeRobot bridge rejected command").toStdString();
        setError(reason);
        if (channel == Channel::Motion) motion_state_ = domain::ExecutionState::Failed;
        if (channel == Channel::Positioning) positioning_state_ = domain::ExecutionState::Failed;
    }

    void failActive(std::string reason) {
        if (motion_state_ == domain::ExecutionState::Running) {
            motion_state_ = domain::ExecutionState::Failed;
        }
        if (positioning_state_ == domain::ExecutionState::Running) {
            positioning_state_ = domain::ExecutionState::Failed;
        }
        setError(std::move(reason));
    }

    void setError(std::string reason) { error_ = std::move(reason); }

    LeRobotProcessConfig config_;
    QProcess process_;
    bool ready_{};
    bool calibrated_{};
    domain::ExecutionState motion_state_{domain::ExecutionState::Idle};
    domain::ExecutionState positioning_state_{domain::ExecutionState::Idle};
    std::uint64_t next_request_id_{1};
    std::unordered_map<std::string, Channel> pending_;
    Clock::time_point last_heartbeat_{Clock::now()};
    std::string error_;
};

LeRobotMotionAdapter::LeRobotMotionAdapter(LeRobotProcessConfig config)
    : implementation_(std::make_unique<Impl>(std::move(config))) {}

LeRobotMotionAdapter::~LeRobotMotionAdapter() = default;

bool LeRobotMotionAdapter::start(const domain::MotionPlan& plan) {
    return implementation_->startMotion(plan);
}

bool LeRobotMotionAdapter::start(const domain::PositioningStep& step) {
    return implementation_->startPositioning(step);
}

domain::ExecutionState LeRobotMotionAdapter::pollMotion() {
    return implementation_->poll(Channel::Motion);
}

domain::ExecutionState LeRobotMotionAdapter::pollPositioning() {
    return implementation_->poll(Channel::Positioning);
}

bool LeRobotMotionAdapter::pause() { return implementation_->simpleCommand("pause"); }
bool LeRobotMotionAdapter::resume() { return implementation_->simpleCommand("resume"); }
bool LeRobotMotionAdapter::cancel() { return implementation_->cancel(); }

bool LeRobotMotionAdapter::commandManualAxialMovement(double distance_m) {
    return implementation_->manualAxial(distance_m);
}

bool LeRobotMotionAdapter::commandManualRotation(double) {
    implementation_->unsupportedRotation();
    return false;
}

bool LeRobotMotionAdapter::isReady() const { return implementation_->ready(); }
bool LeRobotMotionAdapter::isCalibrated() const { return implementation_->calibrated(); }
const std::string& LeRobotMotionAdapter::lastError() const { return implementation_->error(); }

} // namespace fencing::adapters
