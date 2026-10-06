[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $PluginName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${PluginName}-${ProductVersion}-windows-${Target}"

    # Clean up previous packages
    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${PluginName}-*-windows-*.zip",
            "${ProjectRoot}/release/${PluginName}-*-windows-*.exe"
        )
    }
    Remove-Item @RemoveArgs

    # Create zip archive
    Log-Group "Archiving ${PluginName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    # Build InnoSetup installer
    $pf86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $IsccPath = Join-Path $pf86 "Inno Setup 6" "iscc.exe"

    if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
        Log-Group "Installing InnoSetup via Chocolatey..."
        choco install innosetup -y --no-progress 2>&1 | Out-Null
        $IsccPath = Join-Path $pf86 "Inno Setup 6" "iscc.exe"
        if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
            Write-Warning "InnoSetup not found, skipping installer creation."
            Log-Group
            return
        }
        Log-Group
    }

    Log-Group "Building InnoSetup installer..."
    $IssFile = Join-Path $ProjectRoot "build-aux" "installer.iss"
    $SrcDir = Join-Path $ProjectRoot "release" $Configuration
    $OutDir = Join-Path $ProjectRoot "release"

    $IsccArgs = @(
        "/DPluginName=$PluginName"
        "/DMyAppVersion=$ProductVersion"
        "/DMyAppSourceDir=$SrcDir"
        "/DMyAppOutputDir=$OutDir"
        $IssFile
    )

    Write-Debug "Running iscc with: $IsccArgs"
    $process = Start-Process -FilePath $IsccPath -ArgumentList $IsccArgs -NoNewWindow -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "InnoSetup compilation failed with exit code $($process.ExitCode)"
    }

    $InstallerFile = Get-ChildItem -Path "${OutDir}" -Filter "${PluginName}-*-windows-x64.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($InstallerFile) {
        Write-Information "Created installer: $($InstallerFile.Name)"
    }
    Log-Group
}

Package
