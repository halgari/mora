//! `BSTHashMap<FormID, *mut TESForm>` read-only binding.
//!
//! M1-minimal: lookup and read-only iteration are implemented. Insert,
//! delete, and capacity-growth paths are all out of scope.
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

/// Iterator over every `(form_id, *mut TESForm)` pair in a
/// `FormHashMap`. Walks every bucket in `entries`; for each non-empty
/// bucket follows the `next` chain until hitting the `SENTINEL`
/// terminator.
///
/// The `current: *mut HashMapEntry` field makes `FormHashMapIter`
/// `!Send + !Sync`. This is intentional — the iterator must stay on
/// the thread that holds the read lock (see `iter()`'s safety
/// contract), and raw-pointer auto-non-Send enforces that
/// structurally.
pub struct FormHashMapIter<'a> {
    map: &'a FormHashMap,
    /// Index of the bucket currently being walked.
    bucket: u32,
    /// The next entry to yield. `null_mut` means "start a new bucket
    /// at `self.bucket`".
    current: *mut HashMapEntry,
}

impl<'a> FormHashMapIter<'a> {
    fn advance_to_next_bucket(&mut self) {
        while self.bucket < self.map.capacity {
            let b = unsafe { self.map.entries.add(self.bucket as usize) };
            let b_ref = unsafe { &*b };
            // Empty buckets have next == null (Bethesda convention).
            if !b_ref.next.is_null() {
                self.current = b;
                self.bucket += 1;
                return;
            }
            self.bucket += 1;
        }
        // No more buckets.
        self.current = core::ptr::null_mut();
    }
}

impl<'a> Iterator for FormHashMapIter<'a> {
    type Item = (u32, *mut TESForm);

    fn next(&mut self) -> Option<Self::Item> {
        if self.current.is_null() {
            self.advance_to_next_bucket();
            if self.current.is_null() {
                return None;
            }
        }
        let entry = unsafe { &*self.current };
        let out = (entry.key, entry.value);
        // Advance to next entry in the chain.
        if (entry.next as usize) == SENTINEL {
            // End of chain; force next call to move to the next bucket.
            self.current = core::ptr::null_mut();
        } else {
            self.current = entry.next;
        }
        Some(out)
    }
}

impl FormHashMap {
    /// Walk every form in the map.
    ///
    /// # Safety
    /// * The caller must acquire the map's read lock **before** calling
    ///   `iter()` and must not release it until both the iterator and
    ///   every `*mut TESForm` pointer obtained from it are no longer in
    ///   use. The `'a` lifetime ties the iterator to the *map borrow*;
    ///   it does not borrow the lock, so the compiler will not enforce
    ///   this ordering.
    /// * The map's `entries` pointer must be non-null and point to
    ///   `capacity` contiguous `HashMapEntry` elements (guaranteed by
    ///   SKSE for `allForms` after `kDataLoaded`).
    /// * The caller must not mutate the map while the iterator is live
    ///   (no concurrent insert, delete, or rehash).
    pub unsafe fn iter(&self) -> FormHashMapIter<'_> {
        FormHashMapIter {
            map: self,
            bucket: 0,
            current: core::ptr::null_mut(),
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
    fn iter_walks_synthetic_chain() {
        // Build a 4-bucket table with two entries in bucket 0 (chain) and
        // one entry in bucket 2; buckets 1 and 3 empty.
        let fake_a = 0xAAAA_AAAAusize as *mut TESForm;
        let fake_b = 0xBBBB_BBBBusize as *mut TESForm;
        let fake_c = 0xCCCC_CCCCusize as *mut TESForm;

        // The "extra" entry in bucket 0's chain lives outside the bucket
        // array; allocate it on the heap so we can link to it.
        let mut tail = Box::new(HashMapEntry {
            key: 0x200,
            _pad: 0,
            value: fake_b,
            next: SENTINEL as *mut HashMapEntry,
        });

        let mut buckets: Vec<HashMapEntry> = (0..4)
            .map(|_| HashMapEntry {
                key: 0,
                _pad: 0,
                value: core::ptr::null_mut(),
                next: core::ptr::null_mut(),
            })
            .collect();
        // Bucket 0: entry A with next -> tail (entry B, end-of-chain).
        buckets[0].key = 0x100;
        buckets[0].value = fake_a;
        buckets[0].next = tail.as_mut();
        // Bucket 1: empty (next == null).
        // Bucket 2: entry C, end-of-chain.
        buckets[2].key = 0x300;
        buckets[2].value = fake_c;
        buckets[2].next = SENTINEL as *mut HashMapEntry;
        // Bucket 3: empty (next == null).

        let map = FormHashMap {
            _pad00: 0,
            _pad08: 0,
            capacity: 4,
            free: 0,
            good: 0,
            sentinel: SENTINEL as *const (),
            _alloc_pad: 0,
            entries: buckets.as_mut_ptr(),
        };

        let seen: Vec<(u32, *mut TESForm)> =
            unsafe { map.iter().collect() };
        assert_eq!(
            seen,
            vec![(0x100, fake_a), (0x200, fake_b), (0x300, fake_c)],
            "iteration must visit bucket 0 first (entry A then B via chain), \
             then bucket 2 (entry C). Any other order indicates a bug."
        );
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
