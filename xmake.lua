-- 工作区的xmake.lua
set_project("weasel")

-- 定义全局变量
set_xmakever("2.9.4")
set_languages("c++17")
set_runtimes("MT")  -- 设置运行时库为静态链接，避免/MD与/MT冲突
add_defines("UNICODE", "_UNICODE")
add_defines("WINDOWS")
add_defines("MSVC")
add_defines(
  "VERSION_MAJOR=" .. (os.getenv("VERSION_MAJOR") or "0"),
  "VERSION_MINOR=" .. (os.getenv("VERSION_MINOR") or "0"),
  "VERSION_PATCH=" .. (os.getenv("VERSION_PATCH") or "0"),
  "PRODUCT_VERSION=" .. (os.getenv("PRODUCT_VERSION") or "0.0.0.0")
)

add_includedirs("$(projectdir)/include")
-- 设置Boost库的全局路径：优先 BOOST_ROOT 环境变量，其次 scoop 默认安装路径
boost_root = os.getenv("BOOST_ROOT")
if not boost_root or boost_root == '' then
  local userprofile = os.getenv("USERPROFILE") or ''
  if userprofile ~= '' then
    local scoop_boost = path.join(userprofile, "scoop", "apps", "boost", "current")
    if os.isdir(scoop_boost) then
      boost_root = scoop_boost
    end
  end
end
local boost_available = boost_root and os.isfile(path.join(boost_root, "boost", "version.hpp"))
boost_include_path = boost_available and boost_root or ''
boost_lib_path = boost_available and path.join(boost_root, "stage", "lib") or ''
if boost_available then
  add_includedirs(boost_include_path)
  add_linkdirs(boost_lib_path)
  if not os.isdir(boost_lib_path) then
    print(("warning: %s not found; run setup-build-env.ps1 to build boost static libs first."):format(boost_lib_path))
  end
end

-- raise() belongs to xmake's script domain, not the external description
-- scope. Validate Boost from an on_load callback so missing Boost reports the
-- intended error instead of "unknown interface: raise()".
rule("require_boost")
  on_load(function(target)
    if not boost_available then
      raise("Boost not found! Set BOOST_ROOT (see env.bat.template) or install boost via scoop.")
    end
  end)
rule_end()
add_rules("require_boost")

add_cxflags("/utf-8 /MP /O2 /Oi /Gm- /EHsc /MT /GS /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /external:W3 /Gd /TP")
add_ldflags("/TLBID:1 /DYNAMICBASE /NXCOMPAT")

-- Resolve ATL/MFC from the same Visual Studio family selected by xmake.
-- Never append the Developer Prompt's complete INCLUDE list: xmake already
-- wires the selected MSVC/SDK sysincludes, and importing a stale shell INCLUDE
-- can mix STL/ATL headers from another toolset with xmake's compiler/libs.
local function nonempty_env(name)
  local value = os.getenv(name)
  if value and value ~= '' then
    return value:trim()
  end
  return nil
end

local function vs_major_for_year(vs)
  local majors = {
    ["2015"] = "14",
    ["2017"] = "15",
    ["2019"] = "16",
    ["2022"] = "17"
  }
  return majors[tostring(vs or '')]
end

local function vswhere_range_for_year(vs)
  local major = vs_major_for_year(vs)
  if not major then
    return nil
  end
  return "[" .. major .. ".0," .. tostring(tonumber(major) + 1) .. ".0)"
end

local function find_vctools_with_vswhere(configured_vs)
  local program_files_x86 = nonempty_env("ProgramFiles(x86)")
  if not program_files_x86 then
    return nil
  end

  local vswhere = path.join(program_files_x86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
  if not os.isfile(vswhere) then
    return nil
  end

  local args = {
    "-latest", "-products", "*",
    "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "-property", "installationPath"
  }
  local version_range = vswhere_range_for_year(configured_vs)
  if version_range then
    table.insert(args, "-version")
    table.insert(args, version_range)
  end

  local ok, output = pcall(function()
    return os.iorunv(vswhere, args)
  end)
  if not ok or not output then
    return nil
  end

  local install_dir = output:trim()
  if install_dir == '' then
    return nil
  end
  local default_version_file = path.join(
    install_dir, "VC", "Auxiliary", "Build", "Microsoft.VCToolsVersion.default.txt")
  if not os.isfile(default_version_file) then
    return nil
  end
  local toolset_version = io.readfile(default_version_file):trim()
  if toolset_version == '' then
    return nil
  end
  local toolset_dir = path.join(install_dir, "VC", "Tools", "MSVC", toolset_version)
  return os.isdir(toolset_dir) and toolset_dir or nil
end

local configured_vs = get_config("vs")
local vc_tools_dir = nil
local prompt_vc_tools = nonempty_env("VCToolsInstallDir")
local prompt_vs_version = nonempty_env("VisualStudioVersion")
local configured_vs_major = vs_major_for_year(configured_vs)

-- Use the active Developer Prompt only when it is known to match an explicit
-- xmake --vs selection. Without --vs, xmake defaults to the latest installed
-- VS, so a prompt from an older VS must not silently override that choice.
if prompt_vc_tools and configured_vs_major and prompt_vs_version and
   prompt_vs_version:match("^(%d+)") == configured_vs_major then
  vc_tools_dir = prompt_vc_tools
elseif prompt_vc_tools and configured_vs and tostring(configured_vs) ~= '' and
       tostring(configured_vs) ~= 'auto' then
  print(("warning: ignoring VCToolsInstallDir from Visual Studio %s because xmake selected VS %s")
    :format(prompt_vs_version or "unknown", tostring(configured_vs)))
end

if not vc_tools_dir then
  vc_tools_dir = find_vctools_with_vswhere(configured_vs)
end

-- Last-resort fallback for environments without vswhere. This is only safe
-- when no explicit --vs selection contradicts the prompt.
if not vc_tools_dir and prompt_vc_tools and
   (not configured_vs or tostring(configured_vs) == '' or tostring(configured_vs) == 'auto') then
  vc_tools_dir = prompt_vc_tools
end

local atl_include = ''
local atl_lib_dir = ''
dpi_manifest = ''
if vc_tools_dir then
  local candidate_include = path.join(vc_tools_dir, "atlmfc", "include")
  local candidate_lib = path.join(vc_tools_dir, "atlmfc", "lib")
  if os.isdir(candidate_include) and os.isdir(candidate_lib) then
    atl_include = candidate_include
    atl_lib_dir = candidate_lib
    add_includedirs(atl_include)
    for _, manifest in ipairs({
      path.join(vc_tools_dir, "include", "Manifest", "PerMonitorHighDPIAware.manifest"),
      path.join(atl_include, "Manifest", "PerMonitorHighDPIAware.manifest")}) do
      if os.isfile(manifest) then
        dpi_manifest = manifest
        break
      end
    end
  end
end

local atl_available = atl_include ~= '' and atl_lib_dir ~= ''
rule("require_atl")
  on_load(function(target)
    if is_plat("windows") and not atl_available then
      raise("ATL/MFC not found for xmake's selected MSVC toolchain. Install the ATL/MFC component or configure from the matching Visual Studio Developer Prompt.")
    end
  end)
rule_end()
add_rules("require_atl")

-- DPI manifest：仓库根缺失时尽量从所选 VC toolset 复制一份（各 target 的 add_files 引用仓库根路径）
if dpi_manifest ~= '' and not os.isfile(path.join(os.projectdir(), "PerMonitorHighDPIAware.manifest")) then
  if not pcall(function() os.cp(dpi_manifest, os.projectdir()) end) then
    print(("warning: copy %s to the repo root for DPI manifest embedding."):format(dpi_manifest))
  end
end

add_includedirs("$(projectdir)/include/wtl")

if is_arch("x64") then
  add_linkdirs("$(projectdir)/lib64")
  if atl_available then add_linkdirs(path.join(atl_lib_dir, "x64")) end
elseif is_arch("x86") then
  add_linkdirs("$(projectdir)/lib")
  if atl_available then add_linkdirs(path.join(atl_lib_dir, "x86")) end
elseif is_arch("arm") then
  if atl_available then add_linkdirs(path.join(atl_lib_dir, "arm")) end
elseif is_arch("arm64") then
  if atl_available then add_linkdirs(path.join(atl_lib_dir, "arm64")) end
end

add_links("atls", "shell32", "advapi32", "gdi32", "user32", "uuid", "ole32")

includes("WeaselIPC", "WeaselUI", "WeaselTSF")

if is_arch("x64") or is_arch("x86") then
  includes("RimeWithWeasel", "WeaselIPCServer", "WeaselServer", "WeaselDeployer")
end

if is_arch("x86") then
  includes("WeaselSetup")
end

if is_mode("debug") then
  includes("test/TestWeaselIPC")
  includes("test/TestResponseParser")
  includes("test/TestPipeChannel")
  includes("test/TestPipeConnectFailure")
  includes("test/TestFindIME")
  includes("test/TestCompartmentUtil")
  includes("test/TestPerUserReg")
  includes("test/TestWeaselUI")
  includes("test/TestDeviceResources")
  includes("test/TestWeaselUILayout")
  includes("test/TestBstrUtil")
  includes("test/TestWeaselSetup")
  includes("test/TestKeyEvent")
  includes("test/TestRegisterTipKeys")
  includes("test/TestInteractionPolicy")
  includes("test/TestPipeSecurityPolicy")
  if is_arch("x64") or is_arch("x86") then
    includes("test/TestServerCommands")
  end
else
  add_cxflags("/GL")
  add_ldflags("/LTCG /INCREMENTAL:NO", {force = true})
end

rule("subcmd")
  on_load(function(target)
    target:add("ldflags", "/SUBSYSTEM:CONSOLE")
  end)
rule("subwin")
  on_load(function(target)
    target:add("ldflags", "/SUBSYSTEM:WINDOWS")
  end)

rule("add_rcfiles")
  on_load(function(target)
    target:add("files", path.join(target:scriptdir(), "*.rc"),
      {defines = {"VERSION_MAJOR=" .. (os.getenv("VERSION_MAJOR") or "0"),
      "VERSION_MINOR=" .. (os.getenv("VERSION_MINOR") or "0"),
      "VERSION_PATCH=" .. (os.getenv("VERSION_PATCH") or "0"),
      "FILE_VERSION=" .. (os.getenv("FILE_VERSION") or "\"0.0.0.0\""),
      "PRODUCT_VERSION=" .. (os.getenv("PRODUCT_VERSION") or "\"0.0.0.0\"")
    }})
  end)
rule("use_weaselconstants")
  on_load(function(target)
    function check_include_weasel_constants_in_dir(dir)
      local files = os.files(path.join(dir, "**.h"))
      table.join2(files, os.files(path.join(dir, "**.cpp")))
      for _, file in ipairs(files) do
        local content = io.readfile(file)
        if content:find('#include%s+"WeaselConstants%.h"') or content:find('#include%s+<WeaselConstants%.h>') then
          return true
        end
      end
      return false
    end
    if check_include_weasel_constants_in_dir(target:scriptdir()) then
      target:add("rcflags", {
        "/dVERSION_MAJOR=" .. (os.getenv("VERSION_MAJOR") or "0"),
        "/dVERSION_MINOR=" .. (os.getenv("VERSION_MINOR") or "0"),
        "/dVERSION_PATCH=" .. (os.getenv("VERSION_PATCH") or "0"),
        "/dFILE_VERSION=" .. (os.getenv("FILE_VERSION") or "\"0.0.0.0\""),
        "/dPRODUCT_VERSION=" .. (os.getenv("PRODUCT_VERSION") or "\"0.0.0.0\"")
      })
    end
  end)
