target("Server.Native")
    set_kind("shared")
    set_group("Server")
    add_files("**.cpp")
    
    add_headerfiles("**.h", "**.hpp", "**.inl")
    set_pcxxheader("ServerPCH.h")
    add_includedirs(
        ".", 
        "../../../build", 
        "../../../vendor"
    )

    add_defines("STEAMNETWORKINGSOCKETS_STATIC_LINK")

    add_deps("Common", "Protocol", "Server.Scripting")

    add_packages(
        "gamenetworkingsockets",
        "mimalloc",
        "spdlog",
        "hopscotch-map",
        "cryptopp",
        "glm",
        "openssl",
        "zlib",
        "nlohmann_json",
        "flecs",
        "entt",
        "microsoft-gsl")

    after_install(function (target)
        -- Server.Native.dll on Windows, libServer.Native.so on Linux
        local name = path.filename(target:targetfile())
        os.cp(target:targetfile(), path.join(target:installdir("launcher"), "server", name))
    end)