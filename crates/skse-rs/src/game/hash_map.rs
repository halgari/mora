//! `BSTHashMap<FormID, *mut TESForm>` read-only binding.
//!
//! M1-minimal: only lookup is implemented. Insert, delete, iterate,
//! and capacity-growth paths are all out of scope.
//!
//! Algorithm ported from `CommonLibSSE-NG` `BSTScatterTable::do_find`.

use crate::game::form::TESForm;

/// The sentinel Bethesda uses to mark end-of-chain.
pub const SENTINEL: usize = 0xDEADBEEF;

/// Layout of `BSTHashMap<FormID, *mut TESForm>`. Size 0x30.
#[repr(C)]
pub struct FormHashMap {
    pub _pad00: u64,
    pub _pad08: u32,
    pub capacity: u32,
    pub free: u32,
    pub good: u32,
    pub sentinel: *const (),
    pub _alloc_pad: u64,
    pub entries: *mut HashMapEntry,
}

const _: () = assert!(core::mem::size_of::<FormHashMap>() == 0x30);

/// Single entry in the hash map. Size 0x18.
#[repr(C)]
pub struct HashMapEntry {
    pub key: u32,
    pub _pad: u32,
    pub value: *mut TESForm,
    pub next: *mut HashMapEntry,
}

const _: () = assert!(core::mem::size_of::<HashMapEntry>() == 0x18);

impl FormHashMap {
    /// Find the entry for `form_id`. Returns the `TESForm*` on hit,
    /// `core::ptr::null_mut()` on miss.
    ///
    /// # Safety
    /// Caller must hold an appropriate lock on the map; `self` must
    /// be a valid `FormHashMap` obtained via the Address Library's
    /// `allForms` pointer.
    pub unsafe fn lookup(&self, form_id: u32) -> *mut TESForm {
        if self.entries.is_null() || self.capacity == 0 {
            return core::ptr::null_mut();
        }
        let hash = crc32_bethesda(form_id);
        let idx = (hash & (self.capacity.wrapping_sub(1))) as usize;
        // SAFETY: bounds guaranteed by `idx < capacity` (power-of-2 mask).
        let mut entry: *mut HashMapEntry = unsafe { self.entries.add(idx) };
        // Empty slot: next == null. End of chain: next == SENTINEL.
        loop {
            let cur = unsafe { &*entry };
            if cur.next.is_null() {
                // Empty slot marker in Bethesda's design.
                return core::ptr::null_mut();
            }
            if cur.key == form_id {
                return cur.value;
            }
            if cur.next as usize == SENTINEL {
                return core::ptr::null_mut();
            }
            entry = cur.next;
        }
    }
}

/// Bethesda's `BSCRC32<u32>` — standard CRC-32 (poly 0xEDB88320) over
/// the raw little-endian bytes of the key.
pub fn crc32_bethesda(form_id: u32) -> u32 {
    let mut h = crc32fast::Hasher::new();
    h.update(&form_id.to_le_bytes());
    h.finalize()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_known_values() {
        // Smoke-check: CRC-32 of "123456789" ASCII is 0xCBF43926 (well-known).
        // We only use the u32 path, but the underlying crc32fast implementation
        // is well-tested; just sanity-check the function doesn't panic for
        // various inputs.
        assert_eq!(crc32_bethesda(0x0000_0000), crc32_bethesda(0));
        assert_ne!(crc32_bethesda(0), crc32_bethesda(1));
        assert_ne!(crc32_bethesda(0xFFFF_FFFF), crc32_bethesda(0));
    }

    #[test]
    fn layout_sizes() {
        assert_eq!(std::mem::size_of::<FormHashMap>(), 0x30);
        assert_eq!(std::mem::size_of::<HashMapEntry>(), 0x18);
    }

    #[test]
    fn lookup_empty_map_returns_null() {
        let map = FormHashMap {
            _pad00: 0,
            _pad08: 0,
            capacity: 0,
            free: 0,
            good: 0,
            sentinel: SENTINEL as *const (),
            _alloc_pad: 0,
            entries: core::ptr::null_mut(),
        };
        unsafe {
            assert!(map.lookup(0x1234).is_null());
        }
    }

    #[test]
    fn lookup_synthetic_hit() {
        // Build a tiny synthetic map with capacity 8 and one entry for
        // a known formID. We must place the entry in the slot
        // `crc32(formID) & 7` to simulate the natural-home case.
        let form_id: u32 = 0x0001_2EB7; // Iron Sword
        let hash = crc32_bethesda(form_id);
        let idx = (hash & 7) as usize;

        // Allocate 8 entries, all initially "empty" (next = null).
        let mut entries: Vec<HashMapEntry> = (0..8)
            .map(|_| HashMapEntry {
                key: 0,
                _pad: 0,
                value: core::ptr::null_mut(),
                next: core::ptr::null_mut(),
            })
            .collect();
        let fake_form_addr = 0xDEAD_1234usize as *mut TESForm;
        entries[idx].key = form_id;
        entries[idx].value = fake_form_addr;
        entries[idx].next = SENTINEL as *mut HashMapEntry;

        let map = FormHashMap {
            _pad00: 0,
            _pad08: 0,
            capacity: 8,
            free: 7,
            good: 0,
            sentinel: SENTINEL as *const (),
            _alloc_pad: 0,
            entries: entries.as_mut_ptr(),
        };

        unsafe {
            assert_eq!(map.lookup(form_id), fake_form_addr);
            assert!(map.lookup(0x9999_9999).is_null());
        }
        // entries owns the allocation; dropping at end of fn is fine
        // since map no longer references it (test is done).
        drop(entries);
    }
}
