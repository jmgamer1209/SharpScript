# Compiles the script standard library and samples with the real C# compiler, so
# "this file is valid C#" stays a verified claim rather than an intention.
#
# Invoked as:  cmake -DDOTNET=... -DTFM=net9.0 -DSOURCE_DIR=... -DWORK_DIR=... -P csharp_compat.cmake

if(NOT DEFINED DOTNET OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "csharp_compat.cmake needs DOTNET, SOURCE_DIR and WORK_DIR")
endif()
if(NOT DEFINED TFM)
    set(TFM "net9.0")
endif()

function(write_project project_dir project_name)
    file(MAKE_DIRECTORY ${project_dir})
    file(WRITE ${project_dir}/Compat.csproj
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <TargetFramework>${TFM}</TargetFramework>
    <!-- Library: the samples each declare their own entry point. -->
    <OutputType>Library</OutputType>
    <Nullable>disable</Nullable>
    <LangVersion>latest</LangVersion>
    <ImplicitUsings>disable</ImplicitUsings>
    <AssemblyName>${project_name}</AssemblyName>
  </PropertyGroup>
</Project>
")
    # The host API used by samples/native.cs; SharpScript registers it natively.
    file(WRITE ${project_dir}/HostStub.cs
"public static class Host
{
    public static void Log(string message) { System.Console.WriteLine(\"[host] \" + message); }
    public static string Now() { return \"00:00:00\"; }
    public static long GetTickCount() { return 0L; }
}
")
    # All of lib/*.cs: Array<T>, List<T>, Dictionary<K,V>.
    file(COPY ${SOURCE_DIR}/lib/ DESTINATION ${project_dir})
endfunction()

function(build_project project_dir label)
    execute_process(
        COMMAND ${DOTNET} build ${project_dir}/Compat.csproj -v quiet --nologo
        WORKING_DIRECTORY ${project_dir}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label}: the C# compiler rejected the scripts\n${output}")
    endif()
    message(STATUS "${label}: compiled as C#")
endfunction()

# ---------------------------------------------------------------- project 1
set(primary ${WORK_DIR}/primary)
file(REMOVE_RECURSE ${primary})
write_project(${primary} primary)
foreach(sample hello oop native)
    file(COPY ${SOURCE_DIR}/samples/${sample}.cs DESTINATION ${primary})
endforeach()
# The hot reload samples define the same types as their v2 counterpart, so only
# the first generation joins the primary project.
file(COPY ${SOURCE_DIR}/samples/hotreload.cs DESTINATION ${primary})
build_project(${primary} "library + hello/oop/native/hotreload")

# ---------------------------------------------------------------- project 2
set(reload ${WORK_DIR}/reload)
file(REMOVE_RECURSE ${reload})
write_project(${reload} reload)
file(COPY ${SOURCE_DIR}/samples/hotreload.v2.cs DESTINATION ${reload})
build_project(${reload} "hotreload.v2")
