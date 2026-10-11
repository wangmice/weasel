target("TestWeaselSetup")
  set_kind("binary")
  add_files("./*.cpp")
  add_files("../../WeaselSetup/SetupUtil.cpp")
  add_rules("subcmd")
  add_links("advapi32")
  -- Avoid Windows' 32-bit installer-name heuristics: this test only uses HKCU.
  add_ldflags("/manifest:embed", "/manifestuac:level='asInvoker'",
              {force = true, expand = false})
