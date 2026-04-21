//! Integration tests for PatchFile postcard serialization.

use mora_core::{FormId, Patch, PatchFile};

#[test]
fn roundtrip_empty_patch_file() {
    let f = PatchFile::new(0xDEAD_BEEF_CAFE_F00D);
    let bytes = f.to_bytes().expect("serialize");
    let back = PatchFile::from_bytes(&bytes).expect("deserialize");
    assert_eq!(back, f);
    assert_eq!(back.patches.len(), 0);
}

#[test]
fn roundtrip_one_add_keyword() {
    let mut f = PatchFile::new(0);
    f.patches.push(Patch::AddKeyword {
        target: FormId(0x0001_2EB7),
        keyword: FormId(0x0001_E718),
    });
    let bytes = f.to_bytes().unwrap();
    let back = PatchFile::from_bytes(&bytes).unwrap();
    assert_eq!(back, f);
}

#[test]
fn bad_magic_rejected() {
    let mut f = PatchFile::new(0);
    f.patches.push(Patch::AddKeyword {
        target: FormId(1),
        keyword: FormId(2),
    });
    let mut bytes = f.to_bytes().unwrap();
    // Corrupt the first byte of the magic.
    bytes[0] = b'X';
    let err = PatchFile::from_bytes(&bytes).unwrap_err();
    assert!(matches!(err, mora_core::patch::PatchFileError::BadMagic(m) if m != *b"MORA"));
}

#[test]
fn future_version_rejected() {
    let mut f = PatchFile::new(0);
    f.version = 999;
    let bytes = f.to_bytes().unwrap();
    let err = PatchFile::from_bytes(&bytes).unwrap_err();
    assert!(matches!(
        err,
        mora_core::patch::PatchFileError::UnsupportedVersion { got: 999, known: 1 }
    ));
}
