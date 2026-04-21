//! LZ4 decompression for SSE compressed records.
//!
//! Record body layout when `RECORD_FLAG_COMPRESSED` is set:
//!
//!   [4-byte LE u32 decompressed_size][LZ4 Frame data]
//!
//! Verified in Task 8 Step 0 against xEdit / CommonLibSSE-NG:
//! - xEdit `lz4io.pas` `selectDecoder`: dispatches on `LZ4S_MAGICNUMBER = $184D2204` → LZ4 Frame.
//! - xEdit `wbImplementation.pas` `TwbMainRecord.DecompressIfNeeded`: reads `PCardinal(dcDataBasePtr)^`
//!   as decompressed size, then passes `dcDataBasePtr + SizeOf(Cardinal)` as compressed input.
//! - Therefore: Block format is NOT used; Frame format with 4-byte prefix is correct.

use std::io::Read;

use crate::reader::{ReadError, le_u32};

#[derive(Debug, thiserror::Error)]
pub enum DecompressError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("lz4 decompression: {0}")]
    Lz4(String),
    #[error("decompressed size mismatch: expected {expected}, got {actual}")]
    SizeMismatch { expected: usize, actual: usize },
}

/// Decompress a SSE-compressed record body.
///
/// `body` is the full record body (including the 4-byte prefix).
/// Returns the decompressed bytes as a `Vec<u8>` of length
/// `decompressed_size`.
pub fn decompress(body: &[u8]) -> Result<Vec<u8>, DecompressError> {
    let (decompressed_size, o) = le_u32(body, 0)?;
    let compressed = &body[o..];

    let mut out = Vec::with_capacity(decompressed_size as usize);
    let mut dec = lz4_flex::frame::FrameDecoder::new(compressed);
    dec.read_to_end(&mut out)
        .map_err(|e| DecompressError::Lz4(e.to_string()))?;

    if out.len() != decompressed_size as usize {
        return Err(DecompressError::SizeMismatch {
            expected: decompressed_size as usize,
            actual: out.len(),
        });
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn make_compressed(payload: &[u8]) -> Vec<u8> {
        let mut compressed = Vec::new();
        {
            let mut enc = lz4_flex::frame::FrameEncoder::new(&mut compressed);
            enc.write_all(payload).unwrap();
            enc.finish().unwrap();
        }
        let mut buf = Vec::new();
        buf.extend_from_slice(&(payload.len() as u32).to_le_bytes());
        buf.extend_from_slice(&compressed);
        buf
    }

    #[test]
    fn roundtrip_small_payload() {
        let payload = b"hello compressed world";
        let body = make_compressed(payload);
        let out = decompress(&body).unwrap();
        assert_eq!(out, payload);
    }

    #[test]
    fn roundtrip_larger_payload() {
        let payload: Vec<u8> = (0..10_000u32).flat_map(|i| i.to_le_bytes()).collect();
        let body = make_compressed(&payload);
        let out = decompress(&body).unwrap();
        assert_eq!(out, payload);
    }

    #[test]
    fn size_mismatch_detected() {
        // Lie about the decompressed size.
        let payload = b"hello";
        let mut compressed = Vec::new();
        {
            let mut enc = lz4_flex::frame::FrameEncoder::new(&mut compressed);
            enc.write_all(payload).unwrap();
            enc.finish().unwrap();
        }
        let mut body = Vec::new();
        body.extend_from_slice(&999u32.to_le_bytes()); // wrong size
        body.extend_from_slice(&compressed);
        match decompress(&body) {
            Err(DecompressError::SizeMismatch {
                expected: 999,
                actual: 5,
            }) => {}
            other => panic!("expected SizeMismatch; got {other:?}"),
        }
    }
}
