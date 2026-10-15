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
add_ldflags("/TLBID:1", "/DYNAMICBASE", "/NXCOMPAT", {force = true})

-- Resolve ATL/MFC from the same Visual Studio family and VC toolset selected
-- by xmake. Never append the Developer Prompt's complete INCLUDE list: xmake
-- already wires the selected MSVC/SDK sysincludes, and importing a stale shell
-- INCLUDE can mix STL/ATL headers from another compiler installation.
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

-- Xmake's --vs_toolset uses platform-toolset semantics (14.3 == v143),
-- whereas VC\Tools\MSVC directory names are concrete compiler versions such
-- as 14.44.35207. Keep those namespaces separate.
local function requested_toolset_family(toolset)
  local value = tostring(toolset or '')
  if value == '' or value == 'auto' then
    return nil
  end
  local aliases = {
    ["v140"] = "14.0",
    ["v141"] = "14.1",
    ["v142"] = "14.2",
    ["v143"] = "14.3"
  }
  return aliases[value:lower()] or value
end

local function toolset_matches(version, requested)
  local family = requested_toolset_family(requested)
  if not family then
    return true
  end
  if not version then
    return false
  end

  -- VS2022's v143 family spans concrete MSVC 14.3x and 14.4x releases.
  -- 14.5x belongs to the next toolset generation, so do not accept it here.
  if family == "14.3" then
    local major, minor = version:match("^(%d+)%.(%d+)")
    local minor_number = tonumber(minor)
    return major == "14" and minor_number and
           minor_number >= 30 and minor_number < 50
  end

  return version:sub(1, #family) == family
end

local configured_vs = get_config("vs")
local configured_vs_toolset = get_config("vs_toolset")

-- Command execution and filesystem mutation are not allowed in xmake's
-- description domain. Resolve the concrete VC/ATL installation from on_load,
-- where os.iorunv, os.dirs, io.readfile and raise are valid script APIs.
rule("weasel_msvc_atl")
  on_load(function(target)
    if target:plat() ~= "windows" then
      return
    end

    local vc_tools_dir = nil
    local prompt_vc_tools = nonempty_env("VCToolsInstallDir")
    local prompt_vs_version = nonempty_env("VisualStudioVersion")
    local configured_vs_major = vs_major_for_year(configured_vs)

    -- Trust a Developer Prompt only when it matches both an explicit --vs
    -- choice and the requested VC toolset family. This prevents a VS2022
    -- prompt using v143 from contaminating an explicit --vs_toolset=14.2 build.
    if prompt_vc_tools and configured_vs_major and prompt_vs_version and
       prompt_vs_version:match("^(%d+)") == configured_vs_major then
      local normalized_prompt = prompt_vc_tools:gsub("[/\\]+$", "")
      local prompt_toolset_version = path.filename(normalized_prompt)
      if toolset_matches(prompt_toolset_version, configured_vs_toolset) then
        vc_tools_dir = normalized_prompt
      else
        print(("warning: ignoring VCToolsInstallDir toolset %s because xmake selected %s")
          :format(prompt_toolset_version or "unknown",
                  tostring(configured_vs_toolset or "auto")))
      end
    elseif prompt_vc_tools and configured_vs and tostring(configured_vs) ~= '' and
           tostring(configured_vs) ~= 'auto' then
      print(("warning: ignoring VCToolsInstallDir from Visual Studio %s because xmake selected VS %s")
        :format(prompt_vs_version or "unknown", tostring(configured_vs)))
    end

    if not vc_tools_dir then
      local program_files_x86 = nonempty_env("ProgramFiles(x86)")
      if program_files_x86 then
        local vswhere = path.join(program_files_x86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if os.isfile(vswhere) then
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
          if ok and output then
            local install_dir = output:trim()
            if install_dir ~= '' then
              local msvc_root = path.join(install_dir, "VC", "Tools", "MSVC")
              local requested_family = requested_toolset_family(configured_vs_toolset)
              if requested_family then
                -- Concrete directory versions do not share the platform
                -- toolset string. Enumerate the selected VS instance and
                -- classify each candidate by toolset family instead.
                local candidates = os.dirs(path.join(msvc_root, "*"))
                table.sort(candidates, function(a, b) return a > b end)
                for _, candidate in ipairs(candidates) do
                  local version = path.filename(candidate:gsub("[/\\]+$", ""))
                  if toolset_matches(version, configured_vs_toolset) and
                     os.isdir(path.join(candidate, "atlmfc", "include")) then
                    vc_tools_dir = candidate
                    break
                  end
                end
              else
                local default_version_file = path.join(
                  install_dir, "VC", "Auxiliary", "Build", "Microsoft.VCToolsVersion.default.txt")
                if os.isfile(default_version_file) then
                  local toolset_version = io.readfile(default_version_file):trim()
                  if toolset_version ~= '' then
                    local candidate = path.join(msvc_root, toolset_version)
                    if os.isdir(candidate) then
                      vc_tools_dir = candidate
                    end
                  end
                end
              end
            end
          end
        end
      end
    end

    -- Last-resort fallback when vswhere is unavailable and no explicit --vs or
    -- --vs_toolset selection contradicts the active prompt.
    if not vc_tools_dir and prompt_vc_tools and
       (not configured_vs or tostring(configured_vs) == '' or tostring(configured_vs) == 'auto') then
      local normalized_prompt = prompt_vc_tools:gsub("[/\\]+$", "")
      local prompt_toolset_version = path.filename(normalized_prompt)
      if toolset_matches(prompt_toolset_version, configured_vs_toolset) then
        vc_tools_dir = normalized_prompt
      end
    end

    if not vc_tools_dir then
      raise(("ATL/MFC toolset root not found for VS %s / toolset %s. Install the matching ATL/MFC component or use the matching Developer Prompt.")
        :format(tostring(configured_vs or "auto"),
                tostring(configured_vs_toolset or "auto")))
    end

    local atl_include = path.join(vc_tools_dir, "atlmfc", "include")
    local atl_lib_dir = path.join(vc_tools_dir, "atlmfc", "lib")
    if not os.isdir(atl_include) or not os.isdir(atl_lib_dir) then
      raise(("ATL/MFC not found under selected VC toolset: %s"):format(vc_tools_dir))
    end

    target:add("includedirs", atl_include)
    local arch_lib = ({
      x64 = "x64",
      x86 = "x86",
      arm = "arm",
      arm64 = "arm64"
    })[target:arch()]
    if arch_lib then
      target:add("linkdirs", path.join(atl_lib_dir, arch_lib))
    end
  end)
rule_end()
add_rules("weasel_msvc_atl")

-- Keep release LTCG compilation enabled everywhere except WeaselTSF. The TSF
-- target intentionally compiles without /GL, but still links against the same
-- release libraries and keeps the global /LTCG link behavior below.
rule("weasel_release_gl")
  on_load(function(target)
    if target:name() ~= "WeaselTSF" then
      target:add("cxflags", "/GL", {force = true})
    end
  end)
rule_end()
if not is_mode("debug") then
  add_rules("weasel_release_gl")
end

-- PerMonitorHighDPIAware.manifest is tracked at the repository root; do not
-- copy SDK/toolset files during xmake description loading.
add_includedirs("$(projectdir)/include/wtl")

if is_arch("x64") then
  add_linkdirs("$(projectdir)/lib64")
elseif is_arch("x86") then
  add_linkdirs("$(projectdir)/lib")
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
    includes("test/TestTrayRecovery")
  end
else
  add_ldflags("/LTCG", "/INCREMENTAL:NO", {force = true})
end

rule("subcmd")
  on_load(function(target)
    target:add("ldflags", "/SUBSYSTEM:CONSOLE", {force = true})
  end)
rule("subwin")
  on_load(function(target)
    target:add("ldflags", "/SUBSYSTEM:WINDOWS", {force = true})
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
