//! Headless UI driver for Doppio: scripted, human-like actions and
//! screenshots without opening a window.
//!
//! Start with `doppio/docs/headless-testing.md`. In short:
//!
//! ```text
//! cargo run -p doppio-ui --features headless --bin doppio-headless -- run flow.json
//! ```
//!
//! * [`protocol`] — scripts, steps, targets and results (plain JSON);
//! * [`reference`] — the action table everything is validated/documented from;
//! * [`Driver`] — the persistent session over the real `App`;
//! * [`cli`] — the `doppio-headless` command line.

mod annotate;
pub mod cli;
mod driver;
pub mod protocol;
pub mod reference;
mod state;

pub use driver::{Config, Driver, Failure};

/// Version of the JSON protocol. Bumped on breaking changes.
pub const PROTOCOL_VERSION: u32 = 1;

/// The editor keeps process-wide UI state (themes, focus, text input), so
/// anything that renders must run one at a time inside a process.
pub fn ui_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    static DEVICE_TEARDOWN: std::sync::OnceLock<()> = std::sync::OnceLock::new();
    // Headless runs create several short-lived engine sessions in one process.
    // Destroying each GPU device on session close keeps the driver from running
    // out of devices; the interactive editor keeps its single device for the
    // process lifetime.
    DEVICE_TEARDOWN.get_or_init(|| std::env::set_var("CAFFEINE_CAPI_DESTROY_DEVICE", "1"));
    LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[cfg(test)]
mod tests;
