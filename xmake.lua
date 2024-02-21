add_rules("mode.release", "mode.debug")

set_languages("cxx20")

target("prices")
    set_kind("binary")
    add_files("prices.cpp")
    if is_arch("x86") then
        add_includedirs("PDCurses","CTP")
        add_linkdirs("PDCurses","CTP")
        add_links("pdcurses","thostmduserapi_se","advapi32","user32")
    elseif is_arch("x64") then
        add_cxxflags("/utf-8")
        add_defines("PDC_FORCE_UTF8")
        add_includedirs("PDCursesMod","CTP672x64")
        add_linkdirs("PDCursesMod","CTP672x64")
        add_links("pdcurses","thostmduserapi_se","advapi32","user32","winmm")
    end

target("TextTrader")
    set_kind("binary")
    add_files("TextTrader.cpp","INIReader/*.cpp")
    add_includedirs("$(scriptdir)")
    if is_arch("x86") then
        add_includedirs("PDCurses","CTP","INIReader")
        add_linkdirs("PDCurses","CTP")
        add_links("pdcurses","thostmduserapi_se","thosttraderapi_se","advapi32","user32")
    elseif is_arch("x64") then
        add_cxxflags("/utf-8")
        add_defines("PDC_FORCE_UTF8")
        add_includedirs("PDCursesMod","CTP672x64","INIReader")
        add_linkdirs("PDCursesMod","CTP672x64")
        add_links("pdcurses","thostmduserapi_se","thosttraderapi_se","advapi32","user32","winmm","kernel32")
    end

