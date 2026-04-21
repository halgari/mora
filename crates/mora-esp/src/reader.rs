//! Byte-reader helpers for parsing ESP binary data.
//!
//! A `Reader` wraps a `&[u8]` and tracks a cursor; methods read
//! little-endian integers and arbitrary byte slices, advancing the
//! cursor.

use crate::signature::Signature;

/// Errors from byte-level reads.
#[derive(Debug, thiserror::Error)]
pub enum ReadError {
    #[error("unexpected end of input (needed {needed} bytes at offset {offset})")]
    Truncated { offset: usize, needed: usize },
}

/// Read a little-endian `u8` at offset. Returns `(value, new_offset)`.
pub fn le_u8(bytes: &[u8], offset: usize) -> Result<(u8, usize), ReadError> {
    if offset >= bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 1 });
    }
    Ok((bytes[offset], offset + 1))
}

pub fn le_u16(bytes: &[u8], offset: usize) -> Result<(u16, usize), ReadError> {
    if offset + 2 > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 2 });
    }
    Ok((
        u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap()),
        offset + 2,
    ))
}

pub fn le_u32(bytes: &[u8], offset: usize) -> Result<(u32, usize), ReadError> {
    if offset + 4 > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 4 });
    }
    Ok((
        u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap()),
        offset + 4,
    ))
}

pub fn le_f32(bytes: &[u8], offset: usize) -> Result<(f32, usize), ReadError> {
    let (u, o) = le_u32(bytes, offset)?;
    Ok((f32::from_bits(u), o))
}

/// Read a 4-byte signature.
pub fn read_signature(bytes: &[u8], offset: usize) -> Result<(Signature, usize), ReadError> {
    if offset + 4 > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 4 });
    }
    let mut buf = [0u8; 4];
    buf.copy_from_slice(&bytes[offset..offset + 4]);
    Ok((Signature(buf), offset + 4))
}

/// Read an exact `n`-byte slice at offset. Returns `(slice, new_offset)`.
pub fn read_bytes<'a>(
    bytes: &'a [u8],
    offset: usize,
    n: usize,
) -> Result<(&'a [u8], usize), ReadError> {
    if offset + n > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: n });
    }
    Ok((&bytes[offset..offset + n], offset + n))
}

/// Read a NUL-terminated string starting at `offset`, within at most
/// `max_len` bytes. Returns `(string_slice, bytes_consumed_including_nul)`.
/// If no NUL is found before `offset + max_len`, returns `Truncated`.
pub fn read_cstr(bytes: &[u8], offset: usize, max_len: usize) -> Result<(&str, usize), ReadError> {
    let available = bytes.len().saturating_sub(offset);
    let search_len = available.min(max_len);
    let slice = &bytes[offset..offset + search_len];
    let nul_pos = slice.iter().position(|&b| b == 0).ok_or(ReadError::Truncated {
        offset,
        needed: max_len + 1,
    })?;
    let s = core::str::from_utf8(&slice[..nul_pos]).map_err(|_| ReadError::Truncated {
        offset,
        needed: nul_pos,
    })?;
    Ok((s, nul_pos + 1))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn le_u32_roundtrip() {
        let bytes = 0x12345678u32.to_le_bytes();
        let (v, n) = le_u32(&bytes, 0).unwrap();
        assert_eq!(v, 0x12345678);
        assert_eq!(n, 4);
    }

    #[test]
    fn le_u16_happy() {
        let bytes = [0x34, 0x12, 0xFF];
        let (v, n) = le_u16(&bytes, 0).unwrap();
        assert_eq!(v, 0x1234);
        assert_eq!(n, 2);
    }

    #[test]
    fn truncation_error() {
        let bytes = [0x01, 0x02];
        match le_u32(&bytes, 0) {
            Err(ReadError::Truncated { needed: 4, offset: 0 }) => {}
            other => panic!("expected Truncated; got {other:?}"),
        }
    }

    #[test]
    fn read_signature_happy() {
        let bytes = b"TES4\0\0\0\0";
        let (sig, n) = read_signature(bytes, 0).unwrap();
        assert_eq!(sig, Signature::new(b"TES4"));
        assert_eq!(n, 4);
    }

    #[test]
    fn read_cstr_happy() {
        let bytes = b"Hello\0\0";
        let (s, consumed) = read_cstr(bytes, 0, 16).unwrap();
        assert_eq!(s, "Hello");
        assert_eq!(consumed, 6); // 5 chars + NUL
    }

    #[test]
    fn read_cstr_no_nul_is_truncated() {
        let bytes = b"noNulHere";
        match read_cstr(bytes, 0, 9) {
            Err(ReadError::Truncated { .. }) => {}
            other => panic!("expected Truncated; got {other:?}"),
        }
    }
}
