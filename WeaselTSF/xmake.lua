target("WeaselTSF")
  set_kind("shared")
  add_files("./*.cpp", "WeaselTSF.def")
  add_rules("add_rcfiles", "use_weaselconstants")
  add_deps("WeaselIPC", "WeaselUI")
  -- Static-library dependencies do not propagate their Windows system links
  -- to this shared library on every MSVC architecture. Mirror the vcxproj
  -- AdditionalDependencies explicitly so ARM32 resolves the desktop SDK APIs.
  add_syslinks(
    "Usp10", "d2d1", "dwrite", "d3d11", "dxgi", "dcomp", "shcore",
    "kernel32", "user32", "gdi32", "winspool", "comdlg32", "advapi32",
    "shell32", "ole32", "oleaut32", "uuid", "odbc32", "odbccp32")
  local fname = ''
  if is_arch("x86") then
    fname = "weasel.dll"
  elseif is_arch("x64") then
    fname = "weaselx64.dll"
  elseif is_arch("arm") then
    fname = "weaselARM.dll"
  elseif is_arch("arm64") then
    fname = "weaselARM64.dll"
  end
  set_filename(fname)

  add_files("$(projectdir)/PerMonitorHighDPIAware.manifest")
  if is_mode("debug") then
    add_shflags("/DEBUG", "/OPT:REF", "/OPT:ICF", {force = true})
  else
    add_shflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LTCG", "/INCREMENTAL:NO", {force = true})
  end
  before_build(function(target)
    local target_dir = path.join(target:targetdir(), target:name())
    if not os.exists(target_dir) then
      os.mkdir(target_dir)
    end
    target:set("targetdir", target_dir)
  end)

  after_build(function(target)
    os.cp(path.join(target:targetdir(), "weasel*.dll"), "$(projectdir)/output")
    os.cp(path.join(target:targetdir(), "weasel*.pdb"), "$(projectdir)/output")
  end)
