#include "astra-sim/common/Logging.hh"

#include <cstdlib>
#include <filesystem>

namespace AstraSim {

std::unordered_set<spdlog::sink_ptr> LoggerFactory::default_sinks;
bool LoggerFactory::null_logger;

const std::unordered_set<spdlog::sink_ptr>& LoggerFactory::get_default_sinks() {
    return default_sinks;
}

std::shared_ptr<spdlog::logger> LoggerFactory::get_logger(
    const std::string& logger_name) {
    constexpr bool ENABLE_DEFAULT_SINK_FOR_OTHER_LOGGERS = true;
    auto logger = spdlog::get(logger_name);
    if (logger == nullptr) {
        logger = spdlog::create_async<spdlog::sinks::null_sink_mt>(logger_name);
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);
    }
    if constexpr (!ENABLE_DEFAULT_SINK_FOR_OTHER_LOGGERS) {
        return logger;
    }
    auto& logger_sinks = logger->sinks();
    for (auto sink : default_sinks) {
        if (std::find(logger_sinks.begin(), logger_sinks.end(), sink) ==
            logger_sinks.end()) {
            logger_sinks.push_back(sink);
        }
    }
    // If null_logger is set to true (most likely, rank != 0) ignore all of the above, 
    // And return a logger that will not do anything.
    if (null_logger) {
        logger->set_level(spdlog::level::off);
    }
    return logger;
}

void LoggerFactory::init(const std::string& log_config_path, int rank) {
    if (log_config_path != "empty") {
        spdlog_setup::from_file(log_config_path);
    }
    init_default_components(rank);
    if (rank != 0) {
        null_logger = false;
    }
}

void LoggerFactory::shutdown(void) {
    default_sinks.clear();
    spdlog::drop_all();
    spdlog::shutdown();
}

void LoggerFactory::init_default_components(int rank) {
    auto sink_color_console =
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink_color_console->set_level(spdlog::level::warn);
    default_sinks.insert(sink_color_console);

    // [GENIE_CHANGE] Since Genie is a multi-process setup, we do not want all of the processes (over)writing the same logfile.
    // Assign one file per process, and use the rank to differentiate.
    std::string logdir = "log";
    const char* output_path = std::getenv("OUTPUT_PATH");
    if (output_path != nullptr && output_path[0] != '\0') {
        std::filesystem::path candidate(output_path);
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_directory(candidate)) {
            logdir = candidate.string() + "/log";
        }
    }

    std::string logfilename = "log.log";
    std::string logname = logdir + "/" + logfilename;
    if (rank != -1) {
        logname = logdir + "/log_" + std::to_string(rank) + ".log";
    }

    auto sink_rotate_out =
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logname, 1024 * 1024 * 10, 10);
    sink_rotate_out->set_level(spdlog::level::trace);
    default_sinks.insert(sink_rotate_out);

    auto sink_rotate_err =
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logdir + "/err.log", 1024 * 1024 * 10, 10);
    sink_rotate_err->set_level(spdlog::level::err);
    default_sinks.insert(sink_rotate_err);

    //spdlog::init_thread_pool(8192, 0);
    spdlog::set_pattern("[%Y-%m-%dT%T%z] [%L] <%n>: %v");
}

}  // namespace AstraSim
