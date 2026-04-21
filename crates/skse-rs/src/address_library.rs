//! Address Library v2 `.bin` format parser.
//!
//! `Data/SKSE/Plugins/versionlib-<runtime>.bin` is produced by
//! meh321's Address Library for Skyrim SE. Plan 3 parses the v2
//! format (AE 1.6.x) exactly; v1 (SE 1.5.97) and CSV (VR) are
//! out of scope for M1.
//!
//! See `docs/src/skse-rs-ffi-reference.md` for the format spec.

use std::path::Path;

/// Errors surfaced by [`AddressLibrary::load`].
#[derive(Debug, thiserror::Error)]
pub enum AddressLibraryError {
    #[error("failed to read address library: {0}")]
    Io(#[from] std::io::Error),
    #[error("unexpected format version {0}; expected 2")]
    UnexpectedFormat(i32),
    #[error("unexpected pointer_size {0}; expected 8")]
    UnexpectedPointerSize(i32),
    #[error("truncated address library (at byte offset {0})")]
    Truncated(usize),
    #[error("id not found: {0}")]
    IdNotFound(u64),
}

/// A loaded, sorted `(id, offset)` address table.
#[derive(Debug)]
pub struct AddressLibrary {
    /// Parsed runtime version from the header: `(major, minor, patch, build)`.
    pub runtime_version: (i32, i32, i32, i32),
    /// Pairs sorted by `id` ascending. Binary-searchable.
    pairs: Vec<(u64, u64)>,
}

impl AddressLibrary {
    /// Load and parse the v2 `.bin` at `path`. On success, pairs are
    /// sorted by id (in-file order is monotonically increasing due to
    /// the delta encoding with id-mode 1..=5, but 0/6/7 break that,
    /// so we re-sort defensively).
    pub fn load(path: &Path) -> Result<Self, AddressLibraryError> {
        let bytes = std::fs::read(path)?;
        Self::parse(&bytes)
    }

    /// Parse from an in-memory byte slice. Exposed for unit tests.
    pub fn parse(bytes: &[u8]) -> Result<Self, AddressLibraryError> {
        let mut r = Reader::new(bytes);
        let format = r.i32()?;
        if format != 2 {
            return Err(AddressLibraryError::UnexpectedFormat(format));
        }
        let major = r.i32()?;
        let minor = r.i32()?;
        let patch = r.i32()?;
        let build = r.i32()?;
        let name_len = r.i32()?;
        if name_len < 0 {
            return Err(AddressLibraryError::Truncated(r.pos));
        }
        r.skip(name_len as usize)?;
        let pointer_size = r.i32()?;
        if pointer_size != 8 {
            return Err(AddressLibraryError::UnexpectedPointerSize(pointer_size));
        }
        let count = r.i32()?;
        if count < 0 {
            return Err(AddressLibraryError::Truncated(r.pos));
        }
        let ptr_size = pointer_size as u64;

        let mut pairs: Vec<(u64, u64)> = Vec::with_capacity(count as usize);
        let mut prev_id: u64 = 0;
        let mut prev_offset: u64 = 0;
        for _ in 0..count {
            let type_byte = r.u8()?;
            let lo = type_byte & 0x0F;
            let hi = type_byte >> 4;

            let id: u64 = match lo {
                0 => r.u64()?,
                1 => prev_id + 1,
                2 => prev_id + r.u8()? as u64,
                3 => prev_id - r.u8()? as u64,
                4 => prev_id + r.u16()? as u64,
                5 => prev_id - r.u16()? as u64,
                6 => r.u16()? as u64,
                7 => r.u32()? as u64,
                _ => unreachable!("nibble is 4-bit"),
            };

            let scale = (hi & 0x08) != 0;
            let base = if scale {
                prev_offset / ptr_size
            } else {
                prev_offset
            };
            let offset_mode = hi & 0x07;
            let mut offset: u64 = match offset_mode {
                0 => r.u64()?,
                1 => base + 1,
                2 => base + r.u8()? as u64,
                3 => base - r.u8()? as u64,
                4 => base + r.u16()? as u64,
                5 => base - r.u16()? as u64,
                6 => r.u16()? as u64,
                7 => r.u32()? as u64,
                _ => unreachable!("3-bit"),
            };
            if scale {
                offset *= ptr_size;
            }

            pairs.push((id, offset));
            prev_id = id;
            prev_offset = offset;
        }

        // Defensive sort — modes 0/6/7 can break monotonicity.
        pairs.sort_by_key(|(id, _)| *id);

        Ok(AddressLibrary {
            runtime_version: (major, minor, patch, build),
            pairs,
        })
    }

    /// Resolve an id to its offset (rva from the game's image base).
    pub fn resolve(&self, id: u64) -> Result<u64, AddressLibraryError> {
        self.pairs
            .binary_search_by_key(&id, |(i, _)| *i)
            .map(|idx| self.pairs[idx].1)
            .map_err(|_| AddressLibraryError::IdNotFound(id))
    }

    /// Number of parsed pairs. For diagnostics.
    pub fn len(&self) -> usize {
        self.pairs.len()
    }

    /// Empty? (Unusual but possible with count = 0.)
    pub fn is_empty(&self) -> bool {
        self.pairs.is_empty()
    }
}

/// Minimal endianness-aware byte reader.
struct Reader<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Reader { bytes, pos: 0 }
    }

    fn check(&self, n: usize) -> Result<(), AddressLibraryError> {
        if self.pos + n > self.bytes.len() {
            Err(AddressLibraryError::Truncated(self.pos))
        } else {
            Ok(())
        }
    }

    fn u8(&mut self) -> Result<u8, AddressLibraryError> {
        self.check(1)?;
        let v = self.bytes[self.pos];
        self.pos += 1;
        Ok(v)
    }
    fn u16(&mut self) -> Result<u16, AddressLibraryError> {
        self.check(2)?;
        let v = u16::from_le_bytes(self.bytes[self.pos..self.pos + 2].try_into().unwrap());
        self.pos += 2;
        Ok(v)
    }
    fn u32(&mut self) -> Result<u32, AddressLibraryError> {
        self.check(4)?;
        let v = u32::from_le_bytes(self.bytes[self.pos..self.pos + 4].try_into().unwrap());
        self.pos += 4;
        Ok(v)
    }
    fn u64(&mut self) -> Result<u64, AddressLibraryError> {
        self.check(8)?;
        let v = u64::from_le_bytes(self.bytes[self.pos..self.pos + 8].try_into().unwrap());
        self.pos += 8;
        Ok(v)
    }
    fn i32(&mut self) -> Result<i32, AddressLibraryError> {
        Ok(self.u32()? as i32)
    }
    fn skip(&mut self, n: usize) -> Result<(), AddressLibraryError> {
        self.check(n)?;
        self.pos += n;
        Ok(())
    }
}
