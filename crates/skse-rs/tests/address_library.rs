//! Integration tests for the Address Library v2 bin parser.
//!
//! Fixture is hand-encoded inline — a 3-pair table with a variety of
//! encoding modes, so the parser exercises the full delta-decode state
//! machine.

use skse_rs::address_library::{AddressLibrary, AddressLibraryError};

/// Hand-crafted v2 bin: format=2, version=1.6.1170.0, name=("AE"),
/// pointer_size=8, count=3. Pairs:
///   (id=0x0000_0001, offset=0x0000_1000) — id mode 7 (abs u32), offset mode 0 (u64)
///   (id=0x0000_0002, offset=0x0000_1008) — id mode 1 (+1),        offset mode 2 (+1, scaled)
///   (id=0x0000_0400, offset=0x0000_2000) — id mode 4 (+u16 delta), offset mode 7 (abs u32)
fn fixture_bytes() -> Vec<u8> {
    let mut b = Vec::new();
    // header
    b.extend_from_slice(&2i32.to_le_bytes()); // format_version
    b.extend_from_slice(&1i32.to_le_bytes()); // major
    b.extend_from_slice(&6i32.to_le_bytes()); // minor
    b.extend_from_slice(&1170i32.to_le_bytes()); // patch
    b.extend_from_slice(&0i32.to_le_bytes()); // build
    b.extend_from_slice(&2i32.to_le_bytes()); // name_len
    b.extend_from_slice(b"AE");               // name (ignored by parser)
    b.extend_from_slice(&8i32.to_le_bytes()); // pointer_size
    b.extend_from_slice(&3i32.to_le_bytes()); // count

    // pair 1: id mode 7 (abs u32), offset mode 0 (abs u64), no scale
    // type byte: hi=0, lo=7  → 0x07
    b.push(0x07);
    b.extend_from_slice(&0x0000_0001u32.to_le_bytes());
    b.extend_from_slice(&0x0000_1000u64.to_le_bytes());

    // pair 2: id mode 1 (+1), offset mode 2 (+u8, scaled by pointer_size)
    // type byte: hi = 8 (scale) | 2 = 0xA, lo = 1 → 0xA1
    // prev_offset was 0x1000; scale base = 0x1000/8 = 0x200; + u8(1) = 0x201; * 8 = 0x1008
    b.push(0xA1);
    b.push(1u8); // u8 delta for offset

    // pair 3: id mode 4 (+u16 delta), offset mode 7 (abs u32), no scale
    // type byte: hi=7, lo=4 → 0x74
    // prev_id = 2, delta u16 = 0x3FE → id = 0x400
    b.push(0x74);
    b.extend_from_slice(&0x03FEu16.to_le_bytes());
    b.extend_from_slice(&0x0000_2000u32.to_le_bytes());

    b
}

#[test]
fn parse_hand_crafted_fixture() {
    let bytes = fixture_bytes();
    let lib = AddressLibrary::parse(&bytes).expect("parse ok");
    assert_eq!(lib.runtime_version, (1, 6, 1170, 0));
    assert_eq!(lib.len(), 3);
    assert_eq!(lib.resolve(0x0000_0001).unwrap(), 0x0000_1000);
    assert_eq!(lib.resolve(0x0000_0002).unwrap(), 0x0000_1008);
    assert_eq!(lib.resolve(0x0000_0400).unwrap(), 0x0000_2000);
}

#[test]
fn missing_id_errors() {
    let lib = AddressLibrary::parse(&fixture_bytes()).unwrap();
    match lib.resolve(0x1234_5678) {
        Err(AddressLibraryError::IdNotFound(id)) => assert_eq!(id, 0x1234_5678),
        other => panic!("expected IdNotFound; got {other:?}"),
    }
}

#[test]
fn unexpected_format_rejected() {
    let mut bytes = fixture_bytes();
    bytes[0..4].copy_from_slice(&1i32.to_le_bytes()); // v1 not supported
    match AddressLibrary::parse(&bytes) {
        Err(AddressLibraryError::UnexpectedFormat(1)) => {}
        other => panic!("expected UnexpectedFormat(1); got {other:?}"),
    }
}

#[test]
fn truncated_input_errors() {
    let bytes = fixture_bytes();
    match AddressLibrary::parse(&bytes[..10]) {
        Err(AddressLibraryError::Truncated(_)) => {}
        other => panic!("expected Truncated; got {other:?}"),
    }
}

#[test]
fn unexpected_pointer_size_rejected() {
    let mut bytes = fixture_bytes();
    // Patch pointer_size at the right offset: header is
    // 4 (format) + 16 (version) + 4 (name_len) + 2 (name) = 26
    let ptr_size_pos = 26;
    bytes[ptr_size_pos..ptr_size_pos + 4].copy_from_slice(&4i32.to_le_bytes());
    match AddressLibrary::parse(&bytes) {
        Err(AddressLibraryError::UnexpectedPointerSize(4)) => {}
        other => panic!("expected UnexpectedPointerSize(4); got {other:?}"),
    }
}
