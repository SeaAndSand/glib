local gstreamer_include = {
    "C:/Program Files/gstreamer/1.0/msvc_x86_64/include",
    "C:/Program Files/gstreamer/1.0/msvc_x86_64/include/glib-2.0",
    "C:/Program Files/gstreamer/1.0/msvc_x86_64/lib/glib-2.0/include"
}

local gstreamer_linkdir = "C:/Program Files/gstreamer/1.0/msvc_x86_64/lib"
local gstreamer_links = {"gstreamer-1.0", "glib-2.0", "gobject-2.0", "gmodule-2.0", "gio-2.0"}
local gstreamer_bin = "C:/Program Files/gstreamer/1.0/msvc_x86_64/bin"

local function scan_project_dirs()
    local dirs = {}
    local found_dirs = os.dirs("*")
    for _, dir in ipairs(found_dirs) do
        local name = path.basename(dir)
        if name:match("^%d+$") then
            table.insert(dirs, name)
        end
    end
    table.sort(dirs, function(a, b) return tonumber(a) < tonumber(b) end)
    return dirs
end

for _, dir in ipairs(scan_project_dirs()) do
    target("glib_demo_" .. dir)
        set_kind("binary")
        set_languages("c11")
        add_cxflags("/utf-8")
        add_files(dir .. "/*.c")
        add_files("common/*.c")
        add_includedirs(gstreamer_include)
        add_includedirs("common")
        add_linkdirs(gstreamer_linkdir)
        add_links(gstreamer_links)
        set_targetdir("$(curdir)")
        set_basename("glib_demo_" .. dir)
        after_build(function (target)
            os.setenv("PATH", gstreamer_bin .. ";" .. os.getenv("PATH"))
        end)
    target(dir)
        set_kind("phony")
        add_deps("glib_demo_" .. dir)
end