// test_core_link_smoke.cpp (PR 5, commit 3) — full-object link closure.
//
// A test that links against the static archive pulls ONLY referenced members,
// so it can never prove that ALL production objects link together. This
// executable places $<TARGET_OBJECTS:wrf_sdirk3_core_objects> directly in the
// link, forcing simultaneous resolution of every production TU's symbols
// (duplicate definitions, unresolved externals, ABI drift between TUs).
//
// Deliberately empty: the LINK is the test. Forbidden mitigations (per the
// PR 5 directive): --allow-multiple-definition, ignoring unresolved symbols,
// excluding objects, trimming the manifest, or broad mock libraries.
int main() {
    return 0;
}
