$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$configuration = "Release"
$platform = "x64"

$outputDirectory = Join-Path $projectRoot "bin\$platform\$configuration"
$intermediateDirectory = Join-Path $projectRoot "obj\PluginVideoRecordLoaderIl2Cpp\$platform\$configuration"
$runtimeRoot = Join-Path $env:ProgramFiles "dotnet\shared\Microsoft.NETCore.App"
$sdkRoot = Join-Path $env:ProgramFiles "dotnet\sdk"
$localBuildPropsPath = Join-Path $projectRoot "Local.Build.props"

function Get-LocalBuildPropertyValue
{
    param(
        [string]$propertyName
    )

    if (-not (Test-Path $localBuildPropsPath))
    {
        return $null
    }

    [xml]$localBuildPropsXml = Get-Content $localBuildPropsPath
    $propertyGroup = $localBuildPropsXml.Project.PropertyGroup
    if ($null -eq $propertyGroup)
    {
        return $null
    }

    $propertyNode = $propertyGroup.SelectSingleNode($propertyName)
    if ($null -eq $propertyNode)
    {
        return $null
    }

    return $propertyNode.InnerText
}

$bepInExCoreDirectory = Get-LocalBuildPropertyValue "PluginVideoRecordIl2CppBepInExCoreDir"
if ([string]::IsNullOrWhiteSpace($bepInExCoreDirectory))
{
    $bepInExCoreDirectory = $env:PLUGIN_VIDEO_RECORD_IL2CPP_BEPINEX_CORE_DIR
}

if ([string]::IsNullOrWhiteSpace($bepInExCoreDirectory))
{
    throw "未配置 IL2CPP BepInEx core 目录。请创建 Local.Build.props 或设置环境变量 PLUGIN_VIDEO_RECORD_IL2CPP_BEPINEX_CORE_DIR。"
}

$bepInExCoreDirectory = (Resolve-Path $bepInExCoreDirectory).Path

$runtimeDirectory = Get-ChildItem $runtimeRoot -Directory |
    Where-Object { $_.Name -like "6.*" } |
    Sort-Object Name -Descending |
    Select-Object -First 1

if ($null -eq $runtimeDirectory)
{
    throw "未找到可用的 .NET 6 runtime。"
}

$latestSdkDirectory = Get-ChildItem $sdkRoot -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1

$compilerPath = $null
if ($null -ne $latestSdkDirectory)
{
    $candidateCompilerPath = Join-Path $latestSdkDirectory.FullName "Roslyn\bincore\csc.dll"
    if (Test-Path $candidateCompilerPath)
    {
        $compilerPath = $candidateCompilerPath
    }
}

if ([string]::IsNullOrWhiteSpace($compilerPath))
{
    throw "未找到 Roslyn csc.dll。"
}

if (-not (Test-Path (Join-Path $bepInExCoreDirectory "BepInEx.Core.dll")))
{
    throw "IL2CPP BepInEx core 目录无效，缺少 BepInEx.Core.dll。"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $intermediateDirectory | Out-Null

$compilerArguments = New-Object System.Collections.Generic.List[string]
$compilerArguments.Add("/noconfig")
$compilerArguments.Add("/nostdlib+")
$compilerArguments.Add("/target:library")
$compilerArguments.Add("/langversion:latest")
$compilerArguments.Add("/deterministic+")
$compilerArguments.Add("/optimize+")
$compilerArguments.Add("/debug-")
$compilerArguments.Add("/filealign:512")
$compilerArguments.Add("/out:" + (Join-Path $outputDirectory "PluginVideoRecordLoaderIl2Cpp.dll"))

Get-ChildItem $runtimeDirectory.FullName -Filter *.dll |
    Sort-Object Name |
    ForEach-Object {
        try
        {
            [System.Reflection.AssemblyName]::GetAssemblyName($_.FullName) | Out-Null
            $compilerArguments.Add("/reference:" + $_.FullName)
        }
        catch
        {
        }
    }

foreach ($assemblyName in @(
    "BepInEx.Core.dll",
    "BepInEx.Unity.Common.dll",
    "BepInEx.Unity.IL2CPP.dll",
    "Il2CppInterop.Runtime.dll"))
{
    $compilerArguments.Add("/reference:" + (Join-Path $bepInExCoreDirectory $assemblyName))
}

foreach ($sourcePath in @(
    "PluginVideoRecordLoaderIl2Cpp\PluginVideoRecordLoaderIl2CppPlugin.cs",
    "PluginVideoRecordLoaderIl2Cpp\Properties\AssemblyInfo.cs",
    "PluginVideoRecordLoaderShared\PluginVideoRecordNativeLoader.cs"))
{
    $compilerArguments.Add((Join-Path $projectRoot $sourcePath))
}

& dotnet $compilerPath @compilerArguments
if ($LASTEXITCODE -ne 0)
{
    throw "IL2CPP Loader 编译失败，退出码: $LASTEXITCODE"
}
