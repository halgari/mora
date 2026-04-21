//! Shared fixture builders for mora-esp integration tests.
//!
//! Composes ESP byte buffers in memory via builder structs. Designed
//! for readable test bodies that focus on what's being asserted.
//!
//! Some builder methods (ESL, flags, …) exist as part of the public
//! builder API but aren't called by every test suite. Suppress the
//! dead-code + trait-name lints for test-only ergonomics.
#![allow(dead_code, clippy::should_implement_trait)]

pub const TES4_ESM_FLAG: u32 = 0x0000_0001;
pub const TES4_ESL_FLAG: u32 = 0x0000_0200;

/// Builder for a subrecord (signature + LE u16 size + payload).
pub struct SubrecordBuilder {
    pub signature: [u8; 4],
    pub payload: Vec<u8>,
}

impl SubrecordBuilder {
    pub fn new(sig: &[u8; 4], payload: Vec<u8>) -> Self {
        SubrecordBuilder {
            signature: *sig,
            payload,
        }
    }

    pub fn bytes(&self) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(&self.signature);
        v.extend_from_slice(&(self.payload.len() as u16).to_le_bytes());
        v.extend_from_slice(&self.payload);
        v
    }
}

/// Helper: EDID payload — NUL-terminated string.
pub fn edid_payload(id: &str) -> Vec<u8> {
    let mut v = id.as_bytes().to_vec();
    v.push(0);
    v
}

/// Helper: KWDA payload — concatenated LE u32 FormIDs.
pub fn kwda_payload(ids: &[u32]) -> Vec<u8> {
    let mut v = Vec::with_capacity(ids.len() * 4);
    for &id in ids {
        v.extend_from_slice(&id.to_le_bytes());
    }
    v
}

/// Builder for a record (24-byte header + subrecords).
pub struct RecordBuilder {
    pub signature: [u8; 4],
    pub flags: u32,
    pub form_id: u32,
    pub subrecords: Vec<SubrecordBuilder>,
}

impl RecordBuilder {
    pub fn new(sig: &[u8; 4], form_id: u32) -> Self {
        RecordBuilder {
            signature: *sig,
            flags: 0,
            form_id,
            subrecords: Vec::new(),
        }
    }

    pub fn flag(mut self, flag: u32) -> Self {
        self.flags |= flag;
        self
    }

    pub fn add(mut self, sub: SubrecordBuilder) -> Self {
        self.subrecords.push(sub);
        self
    }

    pub fn bytes(&self) -> Vec<u8> {
        let mut body = Vec::new();
        for sub in &self.subrecords {
            body.extend_from_slice(&sub.bytes());
        }
        let mut v = Vec::new();
        v.extend_from_slice(&self.signature);
        v.extend_from_slice(&(body.len() as u32).to_le_bytes());
        v.extend_from_slice(&self.flags.to_le_bytes());
        v.extend_from_slice(&self.form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&44u16.to_le_bytes()); // version
        v.extend_from_slice(&0u16.to_le_bytes()); // unknown
        v.extend_from_slice(&body);
        v
    }
}

/// Builder for a top-level group (24-byte header + contained records).
pub struct GroupBuilder {
    pub label: [u8; 4],
    pub records: Vec<RecordBuilder>,
}

impl GroupBuilder {
    pub fn new(label: &[u8; 4]) -> Self {
        GroupBuilder {
            label: *label,
            records: Vec::new(),
        }
    }

    pub fn add(mut self, rec: RecordBuilder) -> Self {
        self.records.push(rec);
        self
    }

    pub fn bytes(&self) -> Vec<u8> {
        let mut contents = Vec::new();
        for r in &self.records {
            contents.extend_from_slice(&r.bytes());
        }
        let total_size = 24 + contents.len();
        let mut v = Vec::new();
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&(total_size as u32).to_le_bytes());
        v.extend_from_slice(&self.label);
        v.extend_from_slice(&0u32.to_le_bytes()); // group_type = 0
        v.extend_from_slice(&0u16.to_le_bytes()); // timestamp
        v.extend_from_slice(&0u16.to_le_bytes()); // vc_info
        v.extend_from_slice(&0u32.to_le_bytes()); // unknown
        v.extend_from_slice(&contents);
        v
    }
}

/// Builder for a full plugin: TES4 header + groups.
pub struct PluginBuilder {
    pub tes4_flags: u32,
    pub masters: Vec<String>,
    pub groups: Vec<GroupBuilder>,
}

impl PluginBuilder {
    pub fn new() -> Self {
        PluginBuilder {
            tes4_flags: 0,
            masters: Vec::new(),
            groups: Vec::new(),
        }
    }

    pub fn esm(mut self) -> Self {
        self.tes4_flags |= TES4_ESM_FLAG;
        self
    }

    pub fn esl(mut self) -> Self {
        self.tes4_flags |= TES4_ESL_FLAG;
        self
    }

    pub fn master(mut self, name: &str) -> Self {
        self.masters.push(name.to_string());
        self
    }

    pub fn add_group(mut self, g: GroupBuilder) -> Self {
        self.groups.push(g);
        self
    }

    pub fn bytes(&self) -> Vec<u8> {
        // TES4 header
        let mut tes4_body = Vec::new();
        // HEDR
        tes4_body.extend_from_slice(b"HEDR");
        tes4_body.extend_from_slice(&12u16.to_le_bytes());
        tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
        tes4_body.extend_from_slice(&0u32.to_le_bytes());
        tes4_body.extend_from_slice(&0x800u32.to_le_bytes());
        // Masters
        for m in &self.masters {
            let name_bytes = m.as_bytes();
            tes4_body.extend_from_slice(b"MAST");
            tes4_body.extend_from_slice(&((name_bytes.len() + 1) as u16).to_le_bytes());
            tes4_body.extend_from_slice(name_bytes);
            tes4_body.push(0);
            tes4_body.extend_from_slice(b"DATA");
            tes4_body.extend_from_slice(&8u16.to_le_bytes());
            tes4_body.extend_from_slice(&0u64.to_le_bytes());
        }

        let mut v = Vec::new();
        v.extend_from_slice(b"TES4");
        v.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
        v.extend_from_slice(&self.tes4_flags.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // form_id
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&44u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&tes4_body);

        for g in &self.groups {
            v.extend_from_slice(&g.bytes());
        }
        v
    }
}

impl Default for PluginBuilder {
    fn default() -> Self {
        Self::new()
    }
}

#[test]
fn fixtures_compile() {
    let _ = PluginBuilder::new().esm().master("Skyrim.esm").bytes();
}
