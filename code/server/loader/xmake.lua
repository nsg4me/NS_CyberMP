target("Server.Loader")
    set_kind("binary")
    set_group("Server")
    add_rules("csharp")
    add_files("Server.Loader.csproj")
    add_extrafiles("**.cs")

    add_deps("Server.Scripting", "Server.Native")

    on_build(function (target, opt)
        import("utils.progress")

        local script = target:scriptdir()
        local proj = path.join(script, "Server.Loader.csproj")
        local mode = is_mode("debug") and "Debug" or "Release"
        progress.show(opt.progress, "${color.build.target}build Server.Loader")
        os.run("dotnet build " .. proj .. " -c " .. mode)
        os.run("dotnet publish " .. proj .. " -c " .. mode .. " -o " .. target:targetdir())
    end)

    after_install(function (target)
        -- dotnet publish output; the native launcher stub is Server.Loader.exe on Windows, Server.Loader on Linux
        local files = {"Server.Loader.dll", is_plat("windows") and "Server.Loader.exe" or "Server.Loader",
                       "Server.Loader.runtimeconfig.json", "Server.Loader.deps.json"}
        local server = path.join(target:installdir("launcher"), "server")
        for _, file in ipairs(files) do
            os.cp(path.join(target:installdir("bin"), file), path.join(server, file))
        end
    end)

    on_install(function (target)
        -- dotnet publish output; the native launcher stub is Server.Loader.exe on Windows, Server.Loader on Linux
        local files = {"Server.Loader.dll", is_plat("windows") and "Server.Loader.exe" or "Server.Loader",
                       "Server.Loader.runtimeconfig.json", "Server.Loader.deps.json"}
        for _, file in ipairs(files) do
            os.cp(path.join(target:targetdir(), file), path.join(target:installdir("bin"), file))
        end
    end)