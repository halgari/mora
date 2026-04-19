#pragma once

#include "mora/ext/data_source.h"

#include <string>
#include <vector>

namespace mora_skyrim_compile {

// DataSource that ingests *_KID.ini files from the Skyrim Data
// directory and emits `ini/kid_*` facts into the compile-time FactDB.
//
// Dispatch ordering: KidDataSource depends on SkyrimEspDataSource having
// already populated `LoadCtx::editor_ids_out`. The caller must register
// SkyrimEspDataSource first so ExtensionContext::load_required dispatches
// it first (dispatch is in registration order, see
// src/ext/extension.cpp::load_required).
//
// Scan policy: walks LoadCtx::data_dir non-recursively, picks up every
// file whose filename matches `*_KID.ini` (case-insensitive on the
// suffix). If no *_KID.ini files exist, load() is a no-op — it does
// not emit any facts or diagnostics.
class KidDataSource : public mora::ext::DataSource {
public:
    KidDataSource();
    ~KidDataSource() override;

    std::string_view name() const override;
    std::span<const std::string> provides() const override;
    void load(mora::ext::LoadCtx& ctx, mora::FactDB& out) override;

private:
    std::vector<std::string> provides_;
};

} // namespace mora_skyrim_compile
