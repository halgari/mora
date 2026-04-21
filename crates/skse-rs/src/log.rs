//! File-based logger writing to the SKSE plugin log directory.
//!
//! Path on Windows:
//! `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\<plugin>.log`
//!
//! Created on first write. Overwrites any previous log for the same
//! plugin. On non-Windows platforms (tests), falls back to
//! `$TMPDIR/skse-rs-logs/skse-rs-<plugin>.log`.

use std::fs::{File, OpenOptions};
use std::io::{self, Write};
use std::path::PathBuf;
use std::sync::Mutex;

/// Initialization error for the logger.
#[derive(Debug, thiserror::Error)]
pub enum LogInitError {
    #[error("failed to resolve SKSE log directory: {0}")]
    PathResolution(String),
    #[error("failed to create log file: {0}")]
    FileOpen(#[from] io::Error),
}

/// An opened SKSE log file. Use [`Logger::write_line`] to append a
/// line; the logger flushes after each write (durable on crashes).
pub struct Logger {
    file: Mutex<File>,
    path: PathBuf,
}

impl Logger {
    /// Open (or create) the log for a plugin with the given name.
    pub fn open(plugin_name: &str) -> Result<Self, LogInitError> {
        let path = resolve_log_path(plugin_name)?;
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(LogInitError::FileOpen)?;
        }
        let file = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&path)
            .map_err(LogInitError::FileOpen)?;
        Ok(Logger {
            file: Mutex::new(file),
            path,
        })
    }

    /// Write a single line (no trailing newline needed).
    pub fn write_line(&self, line: &str) -> io::Result<()> {
        let mut guard = self.file.lock().expect("log mutex poisoned");
        writeln!(guard, "{}", line)?;
        guard.flush()
    }

    /// Path the logger is writing to. Useful for diagnostics.
    pub fn path(&self) -> &std::path::Path {
        &self.path
    }
}

/// Resolve the log file path for the given plugin name.
fn resolve_log_path(plugin_name: &str) -> Result<PathBuf, LogInitError> {
    let dir = resolve_log_dir()?;
    Ok(dir.join(format!("{}.log", plugin_name)))
}

/// Resolve the SKSE log *directory*.
#[cfg(windows)]
fn resolve_log_dir() -> Result<PathBuf, LogInitError> {
    use widestring::U16CString;
    use windows_sys::Win32::{
        Foundation::S_OK,
        System::Com::CoTaskMemFree,
        UI::Shell::{FOLDERID_Documents, SHGetKnownFolderPath},
    };

    let mut path_ptr: *mut u16 = core::ptr::null_mut();
    let hr = unsafe {
        SHGetKnownFolderPath(&FOLDERID_Documents, 0, std::ptr::null_mut(), &mut path_ptr)
    };
    if hr != S_OK {
        return Err(LogInitError::PathResolution(format!(
            "SHGetKnownFolderPath(Documents) returned HRESULT 0x{:08X}",
            hr
        )));
    }
    let path_str = unsafe { U16CString::from_ptr_str(path_ptr).to_string_lossy() };
    // Caller owns path_ptr — must CoTaskMemFree.
    unsafe { CoTaskMemFree(path_ptr.cast()) };
    let mut p = PathBuf::from(path_str);
    p.push("My Games");
    p.push("Skyrim Special Edition");
    p.push("SKSE");
    Ok(p)
}

#[cfg(not(windows))]
fn resolve_log_dir() -> Result<PathBuf, LogInitError> {
    // Non-Windows: fall back to tmpdir for unit tests.
    Ok(std::env::temp_dir().join("skse-rs-logs"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn logger_writes_to_file() {
        let name = format!("test-logger-{}", std::process::id());
        let logger = Logger::open(&name).expect("open log");
        logger.write_line("hello").expect("write");
        logger.write_line("world").expect("write");
        let contents = std::fs::read_to_string(logger.path()).expect("read");
        assert_eq!(contents, "hello\nworld\n");
        std::fs::remove_file(logger.path()).ok();
    }

    #[test]
    fn logger_overwrites_existing_file() {
        let name = format!("test-logger-truncate-{}", std::process::id());
        // First session
        let logger1 = Logger::open(&name).unwrap();
        logger1.write_line("first-session").unwrap();
        drop(logger1);
        // Second session — should truncate
        let logger2 = Logger::open(&name).unwrap();
        logger2.write_line("second-session").unwrap();
        let contents = std::fs::read_to_string(logger2.path()).unwrap();
        assert_eq!(contents, "second-session\n");
        std::fs::remove_file(logger2.path()).ok();
    }
}
