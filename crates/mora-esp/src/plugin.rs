//! `EspPlugin` — a single mmapped plugin with parsed TES4 header.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use memmap2::Mmap;

use crate::tes4::{Tes4Error, Tes4Header, parse_tes4};

#[derive(Debug, thiserror::Error)]
pub enum EspPluginError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("tes4: {0}")]
    Tes4(#[from] Tes4Error),
}

/// A single loaded plugin — mmapped bytes + parsed TES4 header.
pub struct EspPlugin {
    pub path: PathBuf,
    pub filename: String,
    pub header: Tes4Header,
    /// The full mmapped byte buffer. `Arc` so slices can outlive the
    /// `EspPlugin` struct during iteration.
    pub bytes: Arc<Mmap>,
}

impl EspPlugin {
    /// Open a plugin file, mmap it, parse the TES4 header.
    pub fn open(path: &Path) -> Result<Self, EspPluginError> {
        let file = std::fs::File::open(path)?;
        // SAFETY: the file is opened read-only and the Mmap lives
        // in an Arc owned by the EspPlugin; memory is released when
        // all Arcs drop. Mora never modifies the mmap.
        let mmap = unsafe { Mmap::map(&file)? };
        let header = parse_tes4(&mmap)?;
        let filename = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("")
            .to_string();
        Ok(EspPlugin {
            path: path.to_path_buf(),
            filename,
            header,
            bytes: Arc::new(mmap),
        })
    }

    pub fn is_esm(&self) -> bool {
        self.header.is_esm()
    }

    pub fn is_esl(&self) -> bool {
        self.header.is_esl()
    }

    pub fn masters(&self) -> &[String] {
        &self.header.masters
    }

    /// The byte slice following the TES4 record — where groups begin.
    pub fn body(&self) -> &[u8] {
        // Re-parse TES4 to find where its record ends. The TES4 record
        // header is 24 bytes; data_size tells us the body length.
        use crate::record::{RECORD_HEADER_SIZE, read_record};
        // We know parse_tes4 succeeded, so read_record won't fail.
        let (_rec, next) = read_record(&self.bytes, 0).expect("tes4 header already parsed");
        let _ = RECORD_HEADER_SIZE;
        &self.bytes[next..]
    }
}

impl std::fmt::Debug for EspPlugin {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("EspPlugin")
            .field("filename", &self.filename)
            .field("is_esm", &self.is_esm())
            .field("is_esl", &self.is_esl())
            .field("masters", &self.header.masters)
            .field("byte_len", &self.bytes.len())
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn build_minimal_esp_bytes(is_esm: bool, masters: &[&str]) -> Vec<u8> {
        let mut subs = Vec::new();
        subs.extend_from_slice(b"HEDR");
        subs.extend_from_slice(&12u16.to_le_bytes());
        subs.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
        subs.extend_from_slice(&0u32.to_le_bytes());
        subs.extend_from_slice(&0x800u32.to_le_bytes());

        for m in masters {
            let name_bytes = m.as_bytes();
            let size = (name_bytes.len() + 1) as u16;
            subs.extend_from_slice(b"MAST");
            subs.extend_from_slice(&size.to_le_bytes());
            subs.extend_from_slice(name_bytes);
            subs.push(0);
            // DATA placeholder (8 bytes)
            subs.extend_from_slice(b"DATA");
            subs.extend_from_slice(&8u16.to_le_bytes());
            subs.extend_from_slice(&0u64.to_le_bytes());
        }

        let mut buf = Vec::new();
        buf.extend_from_slice(b"TES4");
        buf.extend_from_slice(&(subs.len() as u32).to_le_bytes());
        buf.extend_from_slice(&(if is_esm { 1u32 } else { 0u32 }).to_le_bytes());
        buf.extend_from_slice(&0u32.to_le_bytes());
        buf.extend_from_slice(&0u32.to_le_bytes());
        buf.extend_from_slice(&44u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&subs);
        buf
    }

    #[test]
    fn open_minimal_plugin() {
        let tmpdir = std::env::temp_dir().join(format!("mora-esp-test-{}", std::process::id()));
        std::fs::create_dir_all(&tmpdir).unwrap();
        let path = tmpdir.join("Test.esm");
        {
            let mut f = std::fs::File::create(&path).unwrap();
            f.write_all(&build_minimal_esp_bytes(
                true,
                &["Skyrim.esm", "Update.esm"],
            ))
            .unwrap();
        }
        let plugin = EspPlugin::open(&path).unwrap();
        assert_eq!(plugin.filename, "Test.esm");
        assert!(plugin.is_esm());
        assert!(!plugin.is_esl());
        assert_eq!(plugin.masters(), &["Skyrim.esm", "Update.esm"]);
        std::fs::remove_file(&path).ok();
        std::fs::remove_dir(&tmpdir).ok();
    }
}
