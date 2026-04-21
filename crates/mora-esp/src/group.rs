//! Group header (24 bytes) parser + top-level group iteration.

use crate::reader::{le_u16, le_u32, read_signature, ReadError};
use crate::signature::{Signature, GRUP};

pub const GROUP_HEADER_SIZE: usize = 24;

/// A parsed group header.
#[derive(Debug)]
pub struct Group<'a> {
    /// For top-level groups: the record-type signature (e.g. `WEAP`).
    /// For nested groups: a type-specific label (CELL parent id, etc.).
    pub label: [u8; 4],
    /// 0 = top-level; 1..=10 = nested variants.
    pub group_type: u32,
    /// Contents — the bytes after the 24-byte header, up to
    /// `group_size - 24` total.
    pub contents: &'a [u8],
}

impl<'a> Group<'a> {
    pub fn label_signature(&self) -> Signature {
        Signature(self.label)
    }

    pub fn is_top_level(&self) -> bool {
        self.group_type == 0
    }
}

/// Read a group header + contents. Returns `(group, new_offset)`.
/// `new_offset` points to the byte after the full group.
pub fn read_group<'a>(bytes: &'a [u8], offset: usize) -> Result<(Group<'a>, usize), ReadError> {
    let (magic, o) = read_signature(bytes, offset)?;
    if magic != GRUP {
        return Err(ReadError::Truncated {
            offset,
            needed: 4, // abused; magic mismatch is an error we report as truncation
        });
    }
    let (group_size, o) = le_u32(bytes, o)?;
    let mut label = [0u8; 4];
    label.copy_from_slice(&bytes[o..o + 4]);
    let o = o + 4;
    let (group_type, o) = le_u32(bytes, o)?;
    let (_timestamp, o) = le_u16(bytes, o)?;
    let (_vc_info, o) = le_u16(bytes, o)?;
    let (_unknown, o) = le_u32(bytes, o)?;

    let contents_start = o;
    let contents_end = offset + group_size as usize;
    if contents_end > bytes.len() || contents_end < contents_start {
        return Err(ReadError::Truncated {
            offset: contents_start,
            needed: group_size as usize - GROUP_HEADER_SIZE,
        });
    }
    let contents = &bytes[contents_start..contents_end];

    Ok((
        Group {
            label,
            group_type,
            contents,
        },
        contents_end,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_group_header(label: &[u8; 4], group_size: u32, group_type: u32) -> Vec<u8> {
        let mut v = Vec::with_capacity(GROUP_HEADER_SIZE);
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&group_size.to_le_bytes());
        v.extend_from_slice(label);
        v.extend_from_slice(&group_type.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v
    }

    #[test]
    fn parses_empty_top_level_weap_group() {
        // group_size = just the header = 24 bytes
        let buf = make_group_header(b"WEAP", GROUP_HEADER_SIZE as u32, 0);
        let (g, next) = read_group(&buf, 0).unwrap();
        assert_eq!(g.label_signature(), Signature::new(b"WEAP"));
        assert!(g.is_top_level());
        assert_eq!(g.contents.len(), 0);
        assert_eq!(next, GROUP_HEADER_SIZE);
    }

    #[test]
    fn parses_group_with_contents() {
        let payload = b"record bytes here";
        let group_size = (GROUP_HEADER_SIZE + payload.len()) as u32;
        let mut buf = make_group_header(b"WEAP", group_size, 0);
        buf.extend_from_slice(payload);
        let (g, next) = read_group(&buf, 0).unwrap();
        assert_eq!(g.contents, payload);
        assert_eq!(next, GROUP_HEADER_SIZE + payload.len());
    }
}
