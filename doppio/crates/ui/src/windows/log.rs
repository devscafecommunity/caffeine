//! Typed console entries so the Console can filter by source.

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LogLevel {
    Error,
    Log,
    Action,
    Engine,
    Ui,
}

impl LogLevel {
    pub const ALL: [LogLevel; 5] = [
        LogLevel::Error,
        LogLevel::Log,
        LogLevel::Action,
        LogLevel::Engine,
        LogLevel::Ui,
    ];

    pub fn label(self) -> &'static str {
        match self {
            LogLevel::Error => "Errors",
            LogLevel::Log => "Log",
            LogLevel::Action => "Actions",
            LogLevel::Engine => "Engine",
            LogLevel::Ui => "UI",
        }
    }

    pub fn prefix(self) -> &'static str {
        match self {
            LogLevel::Error => "ERR",
            LogLevel::Log => "LOG",
            LogLevel::Action => "ACT",
            LogLevel::Engine => "ENG",
            LogLevel::Ui => "UI",
        }
    }
}

#[derive(Debug, Clone)]
pub struct LogEntry {
    pub level: LogLevel,
    pub text: String,
}

impl LogEntry {
    pub fn new(level: LogLevel, text: impl Into<String>) -> Self {
        Self {
            level,
            text: text.into(),
        }
    }
}
